/*
 * Copyright 2022 iLogtail Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>
#include <unordered_set>

#include "json/value.h"

#include "AppConfig.h"

namespace logtail {

struct ECSMeta {
    bool isValid = false;
    std::string instanceID;
    std::string userID;
    std::string regionID;
};
enum Type {
    CUSTOM,
    ECS,
    ECS_ASSIST,
    LOCAL,
};
struct Hostid {
    std::string id;
    Type type;
};
struct InstanceIdentity {
    bool isReady = false;
    ECSMeta ecsMeta;
    Hostid hostid;
};

std::string GetOsDetail();
std::string GetUsername();
std::string GetHostName();
std::string GetHostIpByHostName();
std::string GetHostIpByInterface(const std::string& intf);
uint32_t GetHostIpValueByInterface(const std::string& intf);
std::string GetHostIp(const std::string& intf = "");
void GetAllPids(std::unordered_set<int32_t>& pids);
bool GetKernelInfo(std::string& kernelRelease, int64_t& kernelVersion);
bool GetRedHatReleaseInfo(std::string& os, int64_t& osVersion, std::string bashPath = "");
bool IsDigitsDotsHostname(const char* hostname);
// GetAnyAvailableIP walks through all interfaces (AF_INET) to find an available IP.
// Priority:
// - IP that does not start with "127.".
// - IP from interface at first.
//
// NOTE: logger must be initialized before calling this.
std::string GetAnyAvailableIP();

bool FetchECSMeta(ECSMeta& metaObj);

class HostIdentifier {
public:
    HostIdentifier();
    static HostIdentifier* Instance() {
        static HostIdentifier sInstance;
        return &sInstance;
    }

    // 注意: 不要在类初始化时调用并缓存结果，因为此时ECS元数据可能尚未就绪
    // 建议在实际使用时再调用此方法
    const InstanceIdentity* GetInstanceIdentity() { return &mInstanceIdentity.getReadBuffer(); }

    bool UpdateInstanceIdentity(const ECSMeta& meta);
    void DumpInstanceIdentity();
    void SetInstanceIdentityReady() {
        mInstanceIdentity.getWriteBuffer() = mInstanceIdentity.getReadBuffer();
        mInstanceIdentity.getWriteBuffer().isReady = true;
        mInstanceIdentity.swap();
    };

private:
    // 从文件获取ecs元数据
    void getInstanceIdentityFromFile();
    // 从云助手获取序列号
    void getSerialNumberFromEcsAssist();
    // 从本地文件获取hostid
    void getLocalHostId();

    void updateHostId();

#if defined(_MSC_VER)
    std::string mEcsAssistMachineIdFile = "C:\\ProgramData\\aliyun\\assist\\hybrid\\machine-id";
#else
    std::string mEcsAssistMachineIdFile = "/usr/local/share/aliyun-assist/hybrid/machine-id";
#endif
    bool mHasTriedToGetSerialNumber = false;
    std::string mSerialNumber;

    DoubleBuffer<InstanceIdentity> mInstanceIdentity;
    Hostid mHostid;

    ECSMeta mMetadata;
    Json::Value mInstanceIdentityJson;
    std::string mInstanceIdentityFile;
    bool mHasGeneratedLocalHostId = false;
    std::string mLocalHostId;
#ifdef __ENTERPRISE__
    friend class EnterpriseConfigProvider;
#endif
};

} // namespace logtail
