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

#include <array>
#include <string>
#include <unordered_set>

#include "json/value.h"

#include "AppConfig.h"
#include "models/StringView.h"

namespace logtail {

struct ECSMeta {
    ECSMeta() = default;

    void SetInstanceID(const std::string& id) { SetID(id, instanceID, instanceIDLen); }

    void SetUserID(const std::string& id) { SetID(id, userID, userIDLen); }

    void SetRegionID(const std::string& id) { SetID(id, regionID, regionIDLen); }

    [[nodiscard]] StringView GetInstanceID() const { return StringView(instanceID.data(), instanceIDLen); }
    [[nodiscard]] StringView GetUserID() const { return StringView(userID.data(), userIDLen); }
    [[nodiscard]] StringView GetRegionID() const { return StringView(regionID.data(), regionIDLen); }

    [[nodiscard]] bool IsValid() const { return instanceIDLen > 0 && userIDLen > 0 && regionIDLen > 0; }

private:
    static const size_t ID_MAX_LENGTH = 128;

    std::array<char, ID_MAX_LENGTH> instanceID{};
    size_t instanceIDLen = 0UL;

    std::array<char, ID_MAX_LENGTH> userID{};
    size_t userIDLen = 0UL;

    std::array<char, ID_MAX_LENGTH> regionID{};
    size_t regionIDLen = 0UL;

    template <size_t N>
    void SetID(const std::string& id, std::array<char, N>& target, size_t& targetLen) {
        targetLen = std::min(id.size(), N - 1);
        std::copy_n(id.begin(), targetLen, target.begin());
        target[targetLen] = '\0';
    }
    friend class InstanceIdentityUnittest;
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
class InstanceIdentity {
public:
    bool IsReady() const { return isReady; }
    bool IsECSValid() const { return ecsMeta.IsValid(); }
    StringView GetEcsInstanceID() const { return ecsMeta.GetInstanceID(); }
    StringView GetEcsUserID() const { return ecsMeta.GetUserID(); }
    StringView GetEcsRegionID() const { return ecsMeta.GetRegionID(); }
    StringView GetHostID() const { return hostid.id; }
    Type GetHostIdType() const { return hostid.type; }

    void SetReady(bool ready) { isReady = ready; }
    void SetECSMeta(const ECSMeta& meta) { ecsMeta = meta; }
    void SetHostID(const Hostid& hostid) { this->hostid = hostid; }

private:
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
        mInstanceIdentity.getWriteBuffer().SetReady(true);
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
