// Copyright 2023 iLogtail Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "config/provider/CommonConfigProvider.h"

#include <json/json.h>

#include <filesystem>
#include <iostream>
#include <random>

#include "Constants.h"
#include "app_config/AppConfig.h"
#include "application/Application.h"
#include "common/LogtailCommonFlags.h"
#include "common/StringTools.h"
#include "common/UUIDUtil.h"
#include "common/version.h"
#include "config_server_pb/v2/agent.pb.h"
#include "logger/Logger.h"
#include "monitor/LogFileProfiler.h"
#include "sdk/Common.h"
#include "sdk/CurlImp.h"
#include "sdk/Exception.h"

using namespace std;

DEFINE_FLAG_INT32(heartBeat_update_interval, "second", 10);

namespace logtail {


void CommonConfigProvider::Init(const string& dir) {
    string sName = "CommonConfigProvider";

    ConfigProvider::Init(dir);

    mSequenceNum = 0;

    const Json::Value& confJson = AppConfig::GetInstance()->GetConfig();

    // configserver path
    if (confJson.isMember("ilogtail_configserver_address") && confJson["ilogtail_configserver_address"].isArray()) {
        for (Json::Value::ArrayIndex i = 0; i < confJson["ilogtail_configserver_address"].size(); ++i) {
            vector<string> configServerAddress
                = SplitString(TrimString(confJson["ilogtail_configserver_address"][i].asString()), ":");

            if (configServerAddress.size() != 2) {
                LOG_WARNING(sLogger,
                            ("ilogtail_configserver_address", "format error")(
                                "wrong address", TrimString(confJson["ilogtail_configserver_address"][i].asString())));
                continue;
            }

            string host = configServerAddress[0];
            int32_t port = atoi(configServerAddress[1].c_str());

            if (port < 1 || port > 65535)
                LOG_WARNING(sLogger, ("ilogtail_configserver_address", "illegal port")("port", port));
            else
                mConfigServerAddresses.push_back(ConfigServerAddress(host, port));
        }

        mConfigServerAvailable = true;
        LOG_INFO(sLogger,
                 ("ilogtail_configserver_address", confJson["ilogtail_configserver_address"].toStyledString()));
    }

    // tags for configserver
    if (confJson.isMember("ilogtail_tags") && confJson["ilogtail_tags"].isObject()) {
        Json::Value::Members members = confJson["ilogtail_tags"].getMemberNames();
        for (Json::Value::Members::iterator it = members.begin(); it != members.end(); it++) {
            mConfigServerTags[*it] = confJson["ilogtail_tags"][*it].asString();
        }
        LOG_INFO(sLogger, ("ilogtail_configserver_tags", confJson["ilogtail_tags"].toStyledString()));
    }

    GetConfigUpdate();

    mThreadRes = async(launch::async, &CommonConfigProvider::CheckUpdateThread, this);
}

void CommonConfigProvider::Stop() {
    {
        lock_guard<mutex> lock(mThreadRunningMux);
        mIsThreadRunning = false;
    }
    mStopCV.notify_one();
    future_status s = mThreadRes.wait_for(chrono::seconds(1));
    if (s == future_status::ready) {
        LOG_INFO(sLogger, (sName, "stopped successfully"));
    } else {
        LOG_WARNING(sLogger, (sName, "forced to stopped"));
    }
}

void CommonConfigProvider::CheckUpdateThread() {
    LOG_INFO(sLogger, (sName, "started"));
    usleep((rand() % 10) * 100 * 1000);
    int32_t lastCheckTime = 0;
    unique_lock<mutex> lock(mThreadRunningMux);
    while (mIsThreadRunning) {
        int32_t curTime = time(NULL);
        if (curTime - lastCheckTime >= INT32_FLAG(heartBeat_update_interval)) {
            GetConfigUpdate();
            lastCheckTime = curTime;
        }
        if (mStopCV.wait_for(lock, chrono::seconds(3), [this]() { return !mIsThreadRunning; })) {
            break;
        }
    }
}

CommonConfigProvider::ConfigServerAddress CommonConfigProvider::GetOneConfigServerAddress(bool changeConfigServer) {
    if (0 == mConfigServerAddresses.size()) {
        return ConfigServerAddress("", -1); // No address available
    }

    // Return a random address
    if (changeConfigServer) {
        random_device rd;
        int tmpId = rd() % mConfigServerAddresses.size();
        while (mConfigServerAddresses.size() > 1 && tmpId == mConfigServerAddressId) {
            tmpId = rd() % mConfigServerAddresses.size();
        }
        mConfigServerAddressId = tmpId;
    }
    return ConfigServerAddress(mConfigServerAddresses[mConfigServerAddressId].host,
                               mConfigServerAddresses[mConfigServerAddressId].port);
}

string CommonConfigProvider::GetInstanceId() {
    return CalculateRandomUUID() + "_" + LogFileProfiler::mIpAddr + "_" + ToString(mStartTime);
}

void CommonConfigProvider::GetAgentAttributes(unordered_map<string, string>& lastAttributes) {
    lastAttributes["version"] = ILOGTAIL_VERSION;
    lastAttributes["ip"] = LogFileProfiler::mIpAddr;
    lastAttributes["hostname"] = LogFileProfiler::mHostname;
    lastAttributes["osDetail"] = LogFileProfiler::mOsDetail;
    lastAttributes["username"] = LogFileProfiler::mUsername;
}

void addConfigInfoToRequest(const std::pair<const string, logtail::ConfigInfo>& configInfo,
                            configserver::proto::v2::ConfigInfo* reqConfig) {
    reqConfig->set_name(configInfo.second.name);
    reqConfig->set_message(configInfo.second.message);
    switch (configInfo.second.status) {
        case UNSET:
            reqConfig->set_status(configserver::proto::v2::ConfigStatus::UNSET);
            break;
        case APPLYING:
            reqConfig->set_status(configserver::proto::v2::ConfigStatus::APPLYING);
            break;
        case APPLIED:
            reqConfig->set_status(configserver::proto::v2::ConfigStatus::APPLIED);
            break;
        case FAILED:
            reqConfig->set_status(configserver::proto::v2::ConfigStatus::FAILED);
            break;
    }
    reqConfig->set_version(configInfo.second.version);
}


void CommonConfigProvider::GetConfigUpdate() {
    if (!GetConfigServerAvailable()) {
        return;
    }
    GetAgentAttributes(mAttributes);
    SendHeartBeatAndFetchConfig();
}

void CommonConfigProvider::SendHeartBeatAndFetchConfig()
{
    if (!GetConfigServerAvailable()) {
        return;
    }
    string heartBeatResp = SendHeartBeat();
    ++mSequenceNum;

    configserver::proto::v2::HeartBeatResponse heartBeatRespPb;
    heartBeatRespPb.ParseFromString(heartBeatResp);

    // fetch pipeline config
    unordered_map<string, ConfigInfo> configInfoMap;
    for (const auto config : heartBeatRespPb.pipeline_config_updates()) {
        ConfigInfo configInfo;
        configInfo.name = config.name();
        configInfo.version = config.version();
        configInfo.detail = config.detail();
        configInfoMap[configInfo.name] = configInfo;
    }
    if (!configInfoMap.empty()) {
        LOG_DEBUG(sLogger, ("fetch pipeline config, config file number", configInfoMap.size()));
        string fetchConfigResponse = FetchPipelineConfig(configInfoMap);
        configserver::proto::v2::FetchConfigResponse fetchConfigResponsePb;
        fetchConfigResponsePb.ParseFromString(fetchConfigResponse);
        if (fetchConfigResponsePb.config_details_size() > 0) {
            UpdateRemoteConfig(fetchConfigResponse);
        }
    }

    // fetch process config
    configInfoMap.clear();
    for (const auto config : heartBeatRespPb.process_config_updates()) {
        ConfigInfo configInfo;
        configInfo.name = config.name();
        configInfo.version = config.version();
        configInfo.detail = config.detail();
        configInfoMap[configInfo.name] = configInfo;
    }
    if (!configInfoMap.empty()) {
        LOG_DEBUG(sLogger, ("fetch process config, config file number", configInfoMap.size()));
        string fetchConfigResponse = FetchProcessConfig(configInfoMap);
        configserver::proto::v2::FetchConfigResponse fetchConfigResponsePb;
        fetchConfigResponsePb.ParseFromString(fetchConfigResponse);
        if (fetchConfigResponsePb.config_details_size() > 0) {
            UpdateRemoteConfig(fetchConfigResponse);
        }
    }

    GetOneConfigServerAddress(true);
}

string CommonConfigProvider::SendHeartBeat()
{
    configserver::proto::v2::HeartBeatRequest heartBeatReq;
    string requestID = sdk::Base64Enconde(string("HeartBeat").append(to_string(time(NULL))));
    heartBeatReq.set_request_id(requestID);
    heartBeatReq.set_sequence_num(mSequenceNum);
    heartBeatReq.set_capabilities(configserver::proto::v2::AcceptsProcessConfig
                                      | configserver::proto::v2::AcceptsPipelineConfig);
    heartBeatReq.set_instance_id(GetInstanceId());
    heartBeatReq.set_agent_type("LoongCollector");
    auto attributes = heartBeatReq.mutable_attributes();
    for (const auto& it : mAttributes) {
        if (it.first == "hostname") {
            attributes->set_hostname(it.second);
        } else if (it.first == "ip") {
            attributes->set_ip(it.second);
        } else if (it.first == "version") {
            attributes->set_version(it.second);
        } else {
            google::protobuf::Map<string, string>* extras = attributes->mutable_extras();
            google::protobuf::Map<string, string>::value_type extra(it.first, it.second);
            extras->insert(extra);
        }
    }

    for (auto tag : GetConfigServerTags()) {
        configserver::proto::v2::AgentGroupTag *agentGroupTag = heartBeatReq.add_tags();
        agentGroupTag->set_name(tag.first);
        agentGroupTag->set_value(tag.second);
    }
    heartBeatReq.set_running_status("running");
    heartBeatReq.set_startup_time(mStartTime);

    for (const auto& configInfo : mPipelineConfigInfoMap) {
        addConfigInfoToRequest(configInfo, heartBeatReq.add_pipeline_configs());
    }
    for (const auto& configInfo : mProcessConfigInfoMap) {
        addConfigInfoToRequest(configInfo, heartBeatReq.add_process_configs());
    }

    for (auto &configInfo : mCommandInfoMap) {
        configserver::proto::v2::CommandInfo *command = heartBeatReq.add_custom_commands();
        command->set_type(configInfo.second.type);
        command->set_name(configInfo.second.name);
        switch (configInfo.second.status) {
        case UNSET:
            command->set_status(configserver::proto::v2::ConfigStatus::UNSET);
        case APPLYING:
            command->set_status(configserver::proto::v2::ConfigStatus::APPLYING);
        case APPLIED:
            command->set_status(configserver::proto::v2::ConfigStatus::APPLIED);
        case FAILED:
            command->set_status(configserver::proto::v2::ConfigStatus::FAILED);
        }
        command->set_message(configInfo.second.message);
    }

    string operation = sdk::CONFIGSERVERAGENT;
    operation.append("/").append("HeartBeat");
    string reqBody;
    heartBeatReq.SerializeToString(&reqBody);
    configserver::proto::v2::HeartBeatResponse emptyResult;
    string emptyResultString;
    emptyResult.SerializeToString(&emptyResultString);
    return SendHttpRequest(operation, reqBody, emptyResultString, "SendHeartBeat");
}

string CommonConfigProvider::SendHttpRequest(const string& operation,
                                             const string& reqBody,
                                             const string& emptyResultString,
                                             const string& configType) {
    ConfigServerAddress configServerAddress = GetOneConfigServerAddress(false);
    map<string, string> httpHeader;
    httpHeader[sdk::CONTENT_TYPE] = sdk::TYPE_LOG_PROTOBUF;
    sdk::HttpMessage httpResponse;
    httpResponse.header[sdk::X_LOG_REQUEST_ID] = "ConfigServer";
    sdk::CurlClient client;

    try {
        client.Send(sdk::HTTP_POST,
                    configServerAddress.host,
                    configServerAddress.port,
                    operation,
                    "",
                    httpHeader,
                    reqBody,
                    INT32_FLAG(sls_client_send_timeout),
                    httpResponse,
                    "",
                    false);
        return httpResponse.content;
    } catch (const sdk::LOGException& e) {
        LOG_WARNING(sLogger,
                    (configType, "fail")("reqBody", reqBody)("errCode", e.GetErrorCode())("errMsg", e.GetMessage())(
                        "host", configServerAddress.host)("port", configServerAddress.port));
        return emptyResultString;
    }
}

string CommonConfigProvider::FetchConfig(const unordered_map<string, ConfigInfo>& configInfoMap,
                                         string configType) {
    configserver::proto::v2::FetchConfigRequest fetchConfigRequest;
    string requestID = sdk::Base64Enconde(configType.append(to_string(time(NULL))));
    fetchConfigRequest.set_request_id(requestID);
    fetchConfigRequest.set_instance_id(GetInstanceId());
    for (const auto& configInfo : configInfoMap) {
        addConfigInfoToRequest(configInfo, fetchConfigRequest.add_req_configs());
    }

    string operation = sdk::CONFIGSERVERAGENT;
    operation.append("/").append(configType);
    string reqBody;
    fetchConfigRequest.SerializeToString(&reqBody);
    configserver::proto::v2::FetchConfigResponse emptyResult;
    string emptyResultString;
    emptyResult.SerializeToString(&emptyResultString);
    return SendHttpRequest(operation, reqBody, emptyResultString, configType);
}

string CommonConfigProvider::FetchPipelineConfig(const unordered_map<string, ConfigInfo>& configInfoMap)
{
    return FetchConfig(configInfoMap, "FetchPipelineConfig");
}

string CommonConfigProvider::FetchProcessConfig(const unordered_map<string, ConfigInfo>& configInfoMap) {
    return FetchConfig(configInfoMap, "FetchProcessConfig");
}

void CommonConfigProvider::UpdateRemoteConfig(const string& fetchConfigResponse) {
    configserver::proto::v2::FetchConfigResponse fetchConfigResponsePb;
    fetchConfigResponsePb.ParseFromString(fetchConfigResponse);

    error_code ec;
    filesystem::create_directories(mSourceDir, ec);
    if (ec) {
        StopUsingConfigServer();
        LOG_ERROR(sLogger,
                  ("failed to create dir for common configs", "stop receiving config from common config server")(
                      "dir", mSourceDir.string())("error code", ec.value())("error msg", ec.message()));
        return;
    }

    lock_guard<mutex> lock(mMux);
    for (const auto& config : fetchConfigResponsePb.config_details()) {
        filesystem::path filePath = mSourceDir / (config.name() + ".yaml");
        filesystem::path tmpFilePath = mSourceDir / (config.name() + ".yaml.new");
        if (config.version() == -1) {
            mConfigNameVersionMap.erase(config.name());
            filesystem::remove(filePath, ec);
        } else {
            string configDetail = config.detail();
            mConfigNameVersionMap[config.name()] = config.version();
            ofstream fout(tmpFilePath);
            if (!fout) {
                LOG_WARNING(sLogger, ("failed to open config file", filePath.string()));
                continue;
            }
            fout << configDetail;

            error_code ec;
            filesystem::rename(tmpFilePath, filePath, ec);
            if (ec) {
                LOG_WARNING(sLogger,
                            ("failed to dump config file",
                                filePath.string())("error code", ec.value())("error msg", ec.message()));
                filesystem::remove(tmpFilePath, ec);
            }
        }
    }
}

} // namespace logtail
