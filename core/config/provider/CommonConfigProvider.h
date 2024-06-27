/*
 * Copyright 2023 iLogtail Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/provider/ConfigProvider.h"

namespace logtail {

enum ConfigStatus {
    UNSET = 0,
    APPLYING = 1,
    APPLIED = 2,
    FAILED = 3,
};

struct ConfigInfo {
    std::string name;
    int64_t version;
    ConfigStatus status;
    std::string message;
    std::string detail;
};

struct CommandInfo {
    std::string type;
    std::string name;
    ConfigStatus status;
    std::string message;
};

class CommonConfigProvider : public ConfigProvider {
public:
    std::filesystem::path mSourceDir;
    mutable std::mutex mMux;
    std::unordered_map<std::string, ConfigInfo> mPipelineConfigInfoMap;
    std::unordered_map<std::string, ConfigInfo> mProcessConfigInfoMap;

    std::unordered_map<std::string, CommandInfo> mCommandInfoMap;
    int64_t mSequenceNum;
    std::unordered_map<std::string, std::string> mAttributes;

    CommonConfigProvider(const CommonConfigProvider&) = delete;
    CommonConfigProvider& operator=(const CommonConfigProvider&) = delete;

    static CommonConfigProvider* GetInstance() {
        static CommonConfigProvider instance;
        return &instance;
    }

    void Init(const std::string& dir) override;
    void Stop() override;

    virtual void FeedbackProcessConfigStatus(std::string name, ConfigInfo status);
    virtual void FeedbackPipelineConfigStatus(std::string name, ConfigInfo status);
    virtual void FeedbackCommandStatus(std::string type, std::string name, CommandInfo status);

protected:
    virtual void SendHeartBeatAndFetchConfig();
    virtual std::string FetchProcessConfig(const std::unordered_map<std::string, ConfigInfo>& configInfoMap);
    virtual std::string FetchPipelineConfig(const std::unordered_map<std::string, ConfigInfo>& configInfoMap);
    
    std::string GetInstanceId();
    virtual void GetAgentAttributes(std::unordered_map<std::string, std::string>& lastAttributes);
    virtual void UpdateRemoteConfig(const std::string& fetchConfigResponse);

private:
    struct ConfigServerAddress {
        ConfigServerAddress() = default;
        ConfigServerAddress(const std::string& config_server_host, const std::int32_t& config_server_port)
            : host(config_server_host), port(config_server_port) {}

        std::string host;
        std::int32_t port;
    };

    CommonConfigProvider() = default;
    ~CommonConfigProvider() = default;

    ConfigServerAddress GetOneConfigServerAddress(bool changeConfigServer);
    const std::unordered_map<std::string, std::string>& GetConfigServerTags() const { return mConfigServerTags; }

    void CheckUpdateThread();
    void GetConfigUpdate();
    bool GetConfigServerAvailable() { return mConfigServerAvailable; }
    void StopUsingConfigServer() { mConfigServerAvailable = false; }

    std::string FetchConfig(const std::unordered_map<std::string, ConfigInfo>& configInfoMap, std::string configType);
    std::string SendHeartBeat();
    std::string SendHttpRequest(const std::string& operation,
                           const std::string& reqBody,
                           const std::string& emptyResultString,
                           const std::string& configType);

    std::vector<ConfigServerAddress> mConfigServerAddresses;
    int mConfigServerAddressId = 0;
    std::unordered_map<std::string, std::string> mConfigServerTags;

    int32_t mStartTime;

    std::future<void> mThreadRes;
    mutable std::mutex mThreadRunningMux;
    bool mIsThreadRunning = true;
    mutable std::condition_variable mStopCV;
    std::unordered_map<std::string, int64_t> mConfigNameVersionMap;
    bool mConfigServerAvailable = false;
};

} // namespace logtail
