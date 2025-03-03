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

#include <memory>

#include "json/json.h"

#include "models/PipelineEventGroup.h"
#include "monitor/metric_models/ReentrantMetricsRecord.h"
#include "pipeline/PipelineContext.h"
#include "pipeline/plugin/instance/PluginInstance.h"
#include "pipeline/plugin/interface/Flusher.h"
#include "pipeline/queue/QueueKey.h"

namespace logtail {

class FlusherInstance : public PluginInstance {
public:
    FlusherInstance(Flusher* plugin, const PluginInstance::PluginMeta& pluginMeta)
        : PluginInstance(pluginMeta), mPlugin(plugin) {}

    const std::string& Name() const override { return mPlugin->Name(); };
    const Flusher* GetPlugin() const { return mPlugin.get(); }

    bool Init(const Json::Value& config, PipelineContext& context, size_t flusherIdx, Json::Value& optionalGoPipeline);
    bool Start() { return mPlugin->Start(); }
    bool Stop(bool isPipelineRemoving) { return mPlugin->Stop(isPipelineRemoving); }
    bool Send(PipelineEventGroup&& g);
    bool FlushAll() { return mPlugin->FlushAll(); }
    QueueKey GetQueueKey() const { return mPlugin->GetQueueKey(); }

private:
    std::unique_ptr<Flusher> mPlugin;

    CounterPtr mInGroupsTotal;
    CounterPtr mInEventsTotal;
    CounterPtr mInSizeBytes;
    TimeCounterPtr mTotalPackageTimeMs;
};

} // namespace logtail
