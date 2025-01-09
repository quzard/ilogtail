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


#include "MachineInfoUtil.h"
#include "unittest/Unittest.h"

namespace logtail {

class InstanceIdentityUnittest : public ::testing::Test {
public:
    void TestECSMeta();
};
UNIT_TEST_CASE(InstanceIdentityUnittest, TestECSMeta);

void InstanceIdentityUnittest::TestECSMeta() {
    {
        ECSMeta meta;
        meta.SetInstanceID("i-1234567890");
        meta.SetUserID("1234567890");
        meta.SetRegionID("cn-hangzhou");
        APSARA_TEST_TRUE(meta.IsValid());
        APSARA_TEST_EQUAL(meta.GetInstanceID().to_string(), "i-1234567890");
        APSARA_TEST_EQUAL(meta.GetUserID().to_string(), "1234567890");
        APSARA_TEST_EQUAL(meta.GetRegionID().to_string(), "cn-hangzhou");
    }
    {
        ECSMeta meta;
        meta.SetInstanceID("");
        meta.SetUserID("1234567890");
        meta.SetRegionID("cn-hangzhou");
        APSARA_TEST_FALSE(meta.IsValid());
    }
}

} // namespace logtail
