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
#include <mutex>
#include <string>
#include <unordered_set>

#include "common/UUIDUtil.h"

namespace logtail {

struct ECSMeta {
    bool isValid = false;
    std::string instanceID;
    std::string userID;
    std::string regionID;
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

class HostIdentifier {
public:
    enum Type {
        CUSTOM,
        ECS,
        ECS_ASSIST,
        SERIAL,
        LOCAL,
    };
    struct Hostid {
        std::string id;
        Type type;
    };
    const HostIdentifier::Hostid& GetHostId() {
        std::lock_guard<std::mutex> lock(mMutex);
        return mHostid;
    }
    const ECSMeta& GetECSMeta() {
        std::lock_guard<std::mutex> lock(mMutex);
        return mMetadata;
    }
    void SetECSMeta(const ECSMeta& meta) {
        std::lock_guard<std::mutex> lock(mMutex);
        mMetadata = meta;
    }
    void SetHostId(const Hostid& hostid) {
        std::lock_guard<std::mutex> lock(mMutex);
        mHostid = hostid;
    }

    HostIdentifier();
    static HostIdentifier* Instance() {
        static HostIdentifier instance;
        return &instance;
    }
    ECSMeta FetchECSMeta();
    void DumpECSMeta();

private:
    std::mutex mMutex;
    Hostid mHostid;
    ECSMeta mMetadata;
    std::string mMetadataStr;

    // 从云助手获取序列号
    std::string GetSerialNumberFromEcsAssist(const std::string& machineIdFile);
    static std::string GetEcsAssistMachineIdFile() {
#if defined(_MSC_VER)
        return "C:\\ProgramData\\aliyun\\assist\\hybrid\\machine-id";
#else
        return "/usr/local/share/aliyun-assist/hybrid/machine-id";
#endif
    }
    std::string GetSerialNumberFromEcsAssist() { return GetSerialNumberFromEcsAssist(GetEcsAssistMachineIdFile()); }
    std::string RandomHostid() {
        static std::string hostId = CalculateRandomUUID();
        return hostId;
    }
    const std::string& GetLocalHostId();

    ECSMeta GetECSMetaFromFile();
};

} // namespace logtail
