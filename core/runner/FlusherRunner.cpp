// Copyright 2024 iLogtail Authors
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

#include "runner/FlusherRunner.h"

#include "app_config/AppConfig.h"
#include "application/Application.h"
#include "common/LogtailCommonFlags.h"
#include "common/StringTools.h"
#include "common/http/HttpRequest.h"
#include "logger/Logger.h"
#include "monitor/LogtailAlarm.h"
#include "pipeline/plugin/interface/HttpFlusher.h"
#include "pipeline/queue/QueueKeyManager.h"
#include "pipeline/queue/SenderQueueItem.h"
#include "pipeline/queue/SenderQueueManager.h"
#include "plugin/flusher/sls/DiskBufferWriter.h"
#include "runner/sink/http/HttpSink.h"
// TODO: temporarily used here
#include "plugin/flusher/sls/PackIdManager.h"
#include "plugin/flusher/sls/SLSClientManager.h"

using namespace std;

DEFINE_FLAG_INT32(check_send_client_timeout_interval, "", 600);
DEFINE_FLAG_BOOL(enable_flow_control, "if enable flow control", true);
DEFINE_FLAG_BOOL(enable_send_tps_smoothing, "avoid web server load burst", true);

static const int SEND_BLOCK_COST_TIME_ALARM_INTERVAL_SECOND = 3;

namespace logtail {

bool FlusherRunner::Init() {
    srand(time(nullptr));
    mThreadRes = async(launch::async, &FlusherRunner::Run, this);
    mLastCheckSendClientTime = time(nullptr);
    LoadModuleConfig(true);
    AppConfig::GetInstance()->RegisterCallback(
        "max_bytes_per_sec", std::bind(&FlusherRunner::LoadModuleConfig, this, std::placeholders::_1));
    return true;
}

bool FlusherRunner::LoadModuleConfig(bool isInit) {
    const auto& localConf = AppConfig::GetInstance()->GetLocalConfig();
    const auto& envConf = AppConfig::GetInstance()->GetEnvConfig();
    const auto& remoteConf = AppConfig::GetInstance()->GetRemoteConfig();
    auto ValidateFn = [](const std::string key, const int32_t value) -> bool {
        if (key == "max_bytes_per_sec") {
            if (value < (int32_t)(1024 * 1024)) {
                return false;
            }
            return true;
        }
        return true;
    };
    if (isInit) {
        auto maxBytePerSec = AppConfig::MergeInt32(AppConfig::GetInstance()->GetMaxBytePerSec(),
                                                   localConf,
                                                   envConf,
                                                   remoteConf,
                                                   "max_bytes_per_sec",
                                                   ValidateFn);
        AppConfig::GetInstance()->SetMaxBytePerSec(maxBytePerSec);
        UpdateSendFlowControl();
        return true;
    }
    auto maxBytePerSec = AppConfig::MergeInt32(
        AppConfig::GetInstance()->GetMaxBytePerSec(), localConf, envConf, remoteConf, "max_bytes_per_sec", ValidateFn);
    AppConfig::GetInstance()->SetMaxBytePerSec(maxBytePerSec);
    UpdateSendFlowControl();

    return true;
}

void FlusherRunner::UpdateSendFlowControl() {
    // when inflow exceed 30MB/s, FlowControl lose precision
    if (AppConfig::GetInstance()->GetMaxBytePerSec() >= 30 * 1024 * 1024) {
        if (mSendFlowControl)
            mSendFlowControl = false;
        if (mSendRandomSleep)
            mSendRandomSleep = false;
    } else {
        mSendRandomSleep = BOOL_FLAG(enable_send_tps_smoothing);
        mSendFlowControl = BOOL_FLAG(enable_flow_control);
    }
    LOG_INFO(sLogger,
             ("send byte per second limit", AppConfig::GetInstance()->GetMaxBytePerSec())(
                 "send flow control", mSendFlowControl ? "enable" : "disable")(
                 "send random sleep", mSendRandomSleep ? "enable" : "disable"));
}

void FlusherRunner::Stop() {
    mIsFlush = true;
    SenderQueueManager::GetInstance()->Trigger();
    future_status s = mThreadRes.wait_for(chrono::seconds(10));
    if (s == future_status::ready) {
        LOG_INFO(sLogger, ("flusher runner", "stopped successfully"));
    } else {
        LOG_WARNING(sLogger, ("flusher runner", "forced to stopped"));
    }
}

void FlusherRunner::DecreaseHttpSendingCnt() {
    --mHttpSendingCnt;
    SenderQueueManager::GetInstance()->Trigger();
}

void FlusherRunner::PushToHttpSink(SenderQueueItem* item, bool withLimit) {
    if (!BOOL_FLAG(enable_full_drain_mode) && item->mFlusher->Name() == "flusher_sls"
        && Application::GetInstance()->IsExiting()) {
        DiskBufferWriter::GetInstance()->PushToDiskBuffer(item, 3);
        SenderQueueManager::GetInstance()->RemoveItem(item->mFlusher->GetQueueKey(), item);
        return;
    }

    int32_t beforeSleepTime = time(NULL);
    while (withLimit && !Application::GetInstance()->IsExiting()
           && GetSendingBufferCount() >= AppConfig::GetInstance()->GetSendRequestConcurrency()) {
        usleep(10 * 1000);
    }
    int32_t afterSleepTime = time(NULL);
    int32_t blockCostTime = afterSleepTime - beforeSleepTime;
    if (blockCostTime > SEND_BLOCK_COST_TIME_ALARM_INTERVAL_SECOND) {
        LOG_WARNING(sLogger,
                    ("sending log group blocked too long because send concurrency reached limit. current "
                     "concurrency used",
                     GetSendingBufferCount())("max concurrency", AppConfig::GetInstance()->GetSendRequestConcurrency())(
                        "blocked time", blockCostTime));
        LogtailAlarm::GetInstance()->SendAlarm(SENDING_COSTS_TOO_MUCH_TIME_ALARM,
                                               "sending log group blocked for too much time, cost "
                                                   + ToString(blockCostTime));
    }

    auto req = static_cast<HttpFlusher*>(item->mFlusher)->BuildRequest(item);
    item->mLastSendTime = time(nullptr);
    req->mEnqueTime = item->mLastSendTime;
    HttpSink::GetInstance()->AddRequest(std::move(req));
    ++mHttpSendingCnt;
}

void FlusherRunner::Run() {
    LOG_INFO(sLogger, ("flusher runner", "started"));
    while (true) {
        int32_t curTime = time(NULL);

        vector<SenderQueueItem*> items;
        SenderQueueManager::GetInstance()->GetAllAvailableItems(items, !Application::GetInstance()->IsExiting());
        if (items.empty()) {
            SenderQueueManager::GetInstance()->Wait(1000);
        } else {
            // smoothing send tps, walk around webserver load burst
            uint32_t bufferPackageCount = items.size();
            if (!Application::GetInstance()->IsExiting() && mSendRandomSleep) {
                int64_t sleepMicroseconds = 0;
                if (bufferPackageCount < 20)
                    sleepMicroseconds = (rand() % 30) * 10000; // 0ms ~ 300ms
                else if (bufferPackageCount < 30)
                    sleepMicroseconds = (rand() % 20) * 10000; // 0ms ~ 200ms
                else if (bufferPackageCount < 40)
                    sleepMicroseconds = (rand() % 10) * 10000; // 0ms ~ 100ms
                if (sleepMicroseconds > 0)
                    usleep(sleepMicroseconds);
            }
        }

        for (auto itr = items.begin(); itr != items.end(); ++itr) {
            int32_t waitTime = curTime - (*itr)->mEnqueTime;
            LOG_DEBUG(sLogger,
                      ("got item from sender queue, item address",
                       *itr)("config-flusher-dst", QueueKeyManager::GetInstance()->GetName((*itr)->mQueueKey))(
                          "wait time", ToString(waitTime))("try cnt", ToString((*itr)->mTryCnt)));

            if (!Application::GetInstance()->IsExiting() && mSendFlowControl) {
                RateLimiter::FlowControl((*itr)->mRawSize, mSendLastTime, mSendLastByte, true);
            }

            Dispatch(*itr);
        }

        // TODO: move the following logic to scheduler
        if ((time(NULL) - mLastCheckSendClientTime) > INT32_FLAG(check_send_client_timeout_interval)) {
            SLSClientManager::GetInstance()->CleanTimeoutClient();
            PackIdManager::GetInstance()->CleanTimeoutEntry();
            mLastCheckSendClientTime = time(NULL);
        }

        if (mIsFlush && SenderQueueManager::GetInstance()->IsAllQueueEmpty()) {
            break;
        }
    }
}

void FlusherRunner::Dispatch(SenderQueueItem* item) {
    switch (item->mFlusher->GetSinkType()) {
        case SinkType::HTTP:
            PushToHttpSink(item);
            break;
        default:
            SenderQueueManager::GetInstance()->RemoveItem(item->mFlusher->GetQueueKey(), item);
            break;
    }
}

} // namespace logtail
