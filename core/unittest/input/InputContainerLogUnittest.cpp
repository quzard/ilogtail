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

#include <json/json.h>

#include <filesystem>
#include <memory>
#include <string>

#include "app_config/AppConfig.h"
#include "common/JsonUtil.h"
#include "file_server/FileServer.h"
#include "input/InputContainerLog.h"
#include "pipeline/Pipeline.h"
#include "pipeline/PipelineContext.h"
#include "unittest/Unittest.h"

DECLARE_FLAG_INT32(default_plugin_log_queue_size);

using namespace std;

namespace logtail {

class InputContainerLogUnittest : public testing::Test {
public:
    void OnSuccessfulInit();
    void OnEnableContainerDiscovery();
    void OnPipelineUpdate();

protected:
    static void SetUpTestCase() { AppConfig::GetInstance()->mPurageContainerMode = true; }
    void SetUp() override {
        ctx.SetConfigName("test_config");
        ctx.SetPipeline(p);
    }

private:
    Pipeline p;
    PipelineContext ctx;
};

void InputContainerLogUnittest::OnSuccessfulInit() {
    unique_ptr<InputContainerLog> input;
    Json::Value configJson, optionalGoPipeline;
    string configStr, errorMsg;

    // only mandatory param
    configStr = R"(
        {
            "Type": "input_container_log",
            "IgnoringStderr": false,
            "IgnoringStdout": true
        }
    )";
    APSARA_TEST_TRUE(ParseJsonTable(configStr, configJson, errorMsg));
    input.reset(new InputContainerLog());
    input->SetContext(ctx);
    input->SetMetricsRecordRef(InputContainerLog::sName, "1");
    APSARA_TEST_TRUE(input->Init(configJson, optionalGoPipeline));

    // valid optional param
    configStr = R"(
        {
            "Type": "input_container_log",
            "EnableContainerDiscovery": true,
            "IgnoringStderr": false,
            "IgnoringStdout": true        
        }
    )";
    APSARA_TEST_TRUE(ParseJsonTable(configStr, configJson, errorMsg));
    input.reset(new InputContainerLog());
    input->SetContext(ctx);
    input->SetMetricsRecordRef(InputContainerLog::sName, "1");
    APSARA_TEST_TRUE(input->Init(configJson, optionalGoPipeline));

    // invalid optional param
    configStr = R"(
        {
            "Type": "input_container_log",
            "EnableContainerDiscovery": "true"
        }
    )";
    APSARA_TEST_TRUE(ParseJsonTable(configStr, configJson, errorMsg));
    input.reset(new InputContainerLog());
    input->SetContext(ctx);
    input->SetMetricsRecordRef(InputContainerLog::sName, "1");
    APSARA_TEST_TRUE(input->Init(configJson, optionalGoPipeline));

    // TailingAllMatchedFiles
    configStr = R"(
        {
            "Type": "input_container_log",
            "TailingAllMatchedFiles": true,
        }
    )";
    APSARA_TEST_TRUE(ParseJsonTable(configStr, configJson, errorMsg));
    input.reset(new InputContainerLog());
    input->SetContext(ctx);
    input->SetMetricsRecordRef(InputContainerLog::sName, "1");
    APSARA_TEST_TRUE(input->Init(configJson, optionalGoPipeline));
    APSARA_TEST_TRUE(input->mFileReader.mTailingAllMatchedFiles);

    configStr = R"(
        {
            "Type": "input_container_log"
        }
    )";
    APSARA_TEST_TRUE(ParseJsonTable(configStr, configJson, errorMsg));
    input.reset(new InputContainerLog());
    input->SetContext(ctx);
    input->SetMetricsRecordRef(InputContainerLog::sName, "1");
    APSARA_TEST_TRUE(input->Init(configJson, optionalGoPipeline));
}

void InputContainerLogUnittest::OnEnableContainerDiscovery() {
    unique_ptr<InputContainerLog> input;
    Json::Value configJson, optionalGoPipelineJson, optionalGoPipeline;
    string configStr, optionalGoPipelineStr, errorMsg;

    configStr = R"(
        {
            "Type": "input_container_log",
            "EnableContainerDiscovery": true,
            "ContainerFilters": {
                "K8sNamespaceRegex": "default"
            }
        }
    )";
    optionalGoPipelineStr = R"(
        {
            "global": {
                "AlwaysOnline": true
            },
            "inputs": [
                {                
                    "type": "metric_docker_file",
                    "detail": {
                        "K8sNamespaceRegex": "default"
                    }
                }
            ]
        }
    )";
    APSARA_TEST_TRUE(ParseJsonTable(configStr, configJson, errorMsg));
    APSARA_TEST_TRUE(ParseJsonTable(optionalGoPipelineStr, optionalGoPipelineJson, errorMsg));
    optionalGoPipelineJson["global"]["DefaultLogQueueSize"] = Json::Value(INT32_FLAG(default_plugin_log_queue_size));
    input.reset(new InputContainerLog());
    input->SetContext(ctx);
    input->SetMetricsRecordRef(InputContainerLog::sName, "1");
    APSARA_TEST_TRUE(input->Init(configJson, optionalGoPipeline));
    APSARA_TEST_TRUE(optionalGoPipelineJson == optionalGoPipeline);
}

void InputContainerLogUnittest::OnPipelineUpdate() {
    Json::Value configJson, optionalGoPipeline;
    InputContainerLog input;
    string configStr, errorMsg;

    configStr = R"(
        {
            "Type": "input_container_log"
        }
    )";
    APSARA_TEST_TRUE(ParseJsonTable(configStr, configJson, errorMsg));
    APSARA_TEST_TRUE(input.Init(configJson, optionalGoPipeline));
    input.SetContext(ctx);

    APSARA_TEST_TRUE(input.Start());
    APSARA_TEST_NOT_EQUAL(nullptr, FileServer::GetInstance()->GetFileReaderConfig("test_config").first);
    APSARA_TEST_NOT_EQUAL(nullptr, FileServer::GetInstance()->GetMultilineConfig("test_config").first);

    APSARA_TEST_TRUE(input.Stop(true));
    APSARA_TEST_EQUAL(nullptr, FileServer::GetInstance()->GetFileReaderConfig("test_config").first);
    APSARA_TEST_EQUAL(nullptr, FileServer::GetInstance()->GetMultilineConfig("test_config").first);
}

UNIT_TEST_CASE(InputContainerLogUnittest, OnSuccessfulInit)
UNIT_TEST_CASE(InputContainerLogUnittest, OnEnableContainerDiscovery)
UNIT_TEST_CASE(InputContainerLogUnittest, OnPipelineUpdate)

} // namespace logtail

UNIT_TEST_MAIN
