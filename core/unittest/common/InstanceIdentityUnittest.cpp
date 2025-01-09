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


#include <array>

#include "Flags.h"
#include "MachineInfoUtil.h"
#include "unittest/Unittest.h"

DECLARE_FLAG_STRING(agent_host_id);

namespace logtail {

class InstanceIdentityUnittest : public ::testing::Test {
public:
    void TestECSMeta();
    void TestUpdateECSMeta();
};

UNIT_TEST_CASE(InstanceIdentityUnittest, TestECSMeta);
UNIT_TEST_CASE(InstanceIdentityUnittest, TestUpdateECSMeta);
void InstanceIdentityUnittest::TestECSMeta() {
    {
        ECSMeta meta;
        meta.SetInstanceID("i-1234567890");
        meta.SetUserID("1234567890");
        meta.SetRegionID("cn-hangzhou");
        APSARA_TEST_TRUE(meta.IsValid());
        APSARA_TEST_EQUAL(meta.GetInstanceID().to_string(), "i-1234567890");
        APSARA_TEST_EQUAL(meta.instanceIDLen, 12);
        APSARA_TEST_EQUAL(meta.GetUserID().to_string(), "1234567890");
        APSARA_TEST_EQUAL(meta.userIDLen, 10);
        APSARA_TEST_EQUAL(meta.GetRegionID().to_string(), "cn-hangzhou");
        APSARA_TEST_EQUAL(meta.regionIDLen, 11);
    }
    {
        ECSMeta meta;
        meta.SetInstanceID("");
        meta.SetUserID("1234567890");
        meta.SetRegionID("cn-hangzhou");
        APSARA_TEST_FALSE(meta.IsValid());
    }
    {
        ECSMeta meta;
        for (size_t i = 0; i < ID_MAX_LENGTH; ++i) {
            APSARA_TEST_EQUAL(meta.instanceID[i], '\0');
            APSARA_TEST_EQUAL(meta.userID[i], '\0');
            APSARA_TEST_EQUAL(meta.regionID[i], '\0');
        }
    }
    {
        ECSMeta meta;
        std::array<char, ID_MAX_LENGTH + 1> testString{};
        for (size_t i = 0; i < testString.size(); ++i) {
            testString[i] = 'a';
        }
        meta.SetInstanceID(testString.data());
        meta.SetUserID(testString.data());
        meta.SetRegionID(testString.data());
        APSARA_TEST_TRUE(meta.IsValid());
        APSARA_TEST_EQUAL(meta.GetInstanceID().to_string(), StringView(testString.data(), ID_MAX_LENGTH - 1));
        APSARA_TEST_EQUAL(meta.GetUserID().to_string(), StringView(testString.data(), ID_MAX_LENGTH - 1));
        APSARA_TEST_EQUAL(meta.GetRegionID().to_string(), StringView(testString.data(), ID_MAX_LENGTH - 1));

        APSARA_TEST_EQUAL(meta.GetInstanceID().size(), ID_MAX_LENGTH - 1);
        APSARA_TEST_EQUAL(meta.GetUserID().size(), ID_MAX_LENGTH - 1);
        APSARA_TEST_EQUAL(meta.GetRegionID().size(), ID_MAX_LENGTH - 1);
    }
}

void InstanceIdentityUnittest::TestUpdateECSMeta() {
    STRING_FLAG(agent_host_id) = "test_host_id";
    {
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetHostID().to_string(),
                          BOOL_FLAG(agent_host_id));
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetHostIdType(), Hostid::Type::CUSTOM);
        APSARA_TEST_TRUE(HostIdentifier::Instance()->GetInstanceIdentity()->IsReady());
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsInstanceID().to_string(), "");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsUserID().to_string(), "");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsRegionID().to_string(), "");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->IsECSValid(), false);
    }
    {
        // 更新合法的ecs meta
        ECSMeta meta;
        meta.SetInstanceID("i-1234567890");
        meta.SetUserID("1234567890");
        meta.SetRegionID("cn-hangzhou");
        HostIdentifier::Instance()->UpdateInstanceIdentity(meta);
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetHostID().to_string(), "i-1234567890");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetHostIdType(), Hostid::Type::ECS);
        APSARA_TEST_TRUE(HostIdentifier::Instance()->GetInstanceIdentity()->IsReady());
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsInstanceID().to_string(),
                          "i-1234567890");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsUserID().to_string(), "1234567890");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsRegionID().to_string(),
                          "cn-hangzhou");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->IsECSValid(), true);
    }
    {
        // 更新不合法的ecs meta时，instanceIdentity不更新
        ECSMeta meta;
        HostIdentifier::Instance()->UpdateInstanceIdentity(meta);
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetHostID().to_string(), "i-1234567890");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetHostIdType(), Hostid::Type::ECS);
        APSARA_TEST_TRUE(HostIdentifier::Instance()->GetInstanceIdentity()->IsReady());
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsInstanceID().to_string(),
                          "i-1234567890");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsUserID().to_string(), "1234567890");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->GetEcsRegionID().to_string(),
                          "cn-hangzhou");
        APSARA_TEST_EQUAL(HostIdentifier::Instance()->GetInstanceIdentity()->IsECSValid(), true);
    }
} // namespace logtail

} // namespace logtail

UNIT_TEST_MAIN
