/*
 * Copyright 2023 iLogtail Authors
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

#include "processor/ProcessorSplitRegexNative.h"

#include <boost/regex.hpp>
#include <string>

#include "app_config/AppConfig.h"
#include "common/Constants.h"
#include "common/ParamExtractor.h"
#include "logger/Logger.h"
#include "models/LogEvent.h"
#include "monitor/MetricConstants.h"
#include "plugin/instance/ProcessorInstance.h"
#include "reader/LogFileReader.h" //SplitState

namespace logtail {

const std::string ProcessorSplitRegexNative::sName = "processor_split_regex_native";

bool ProcessorSplitRegexNative::Init(const Json::Value& config) {
    std::string errorMsg;

    // SourceKey
    if (!GetOptionalStringParam(config, "SourceKey", mSourceKey, errorMsg)) {
        PARAM_WARNING_DEFAULT(mContext->GetLogger(),
                              mContext->GetAlarm(),
                              errorMsg,
                              mSourceKey,
                              sName,
                              mContext->GetConfigName(),
                              mContext->GetProjectName(),
                              mContext->GetLogstoreName(),
                              mContext->GetRegion());
    }

    if (!mMultiline.Init(config, *mContext, sName)) {
        return false;
    }

    // AppendingLogPositionMeta
    if (!GetOptionalBoolParam(config, "AppendingLogPositionMeta", mAppendingLogPositionMeta, errorMsg)) {
        PARAM_WARNING_DEFAULT(mContext->GetLogger(),
                              mContext->GetAlarm(),
                              errorMsg,
                              mAppendingLogPositionMeta,
                              sName,
                              mContext->GetConfigName(),
                              mContext->GetProjectName(),
                              mContext->GetLogstoreName(),
                              mContext->GetRegion());
    }

    mFeedLines = &(GetContext().GetProcessProfile().feedLines);
    mSplitLines = &(GetContext().GetProcessProfile().splitLines);

    return true;
}

void ProcessorSplitRegexNative::Process(PipelineEventGroup& logGroup) {
    StringView fileInode = logGroup.GetMetadata(EventGroupMetaKey::LOG_FILE_INODE);

    if (logGroup.GetEvents().empty()) {
        return;
    }

    // // 尝试获取并处理缓存中的未处理日志
    // auto it = mUnprocessedLogs.find(fileInode);
    // if (it != mUnprocessedLogs.end()) {
    //     // 如果有缓存的日志，将其作为新事件处理
    // }

    EventsContainer newEvents;
    const StringView& logPath = logGroup.GetMetadata(EventGroupMetaKey::LOG_FILE_PATH_RESOLVED);
    for (const PipelineEventPtr& e : logGroup.GetEvents()) {
        ProcessEvent(logGroup, logPath, e, newEvents);
    }

    // 在函数末尾，处理最后的日志并保存到缓存
    // if (!lastEventRemainder.empty()) {
    //     mUnprocessedLogs[fileInode.to_string()] = lastEventRemainder;
    // }

    *mSplitLines = newEvents.size();
    logGroup.SwapEvents(newEvents);

    return;
}

bool ProcessorSplitRegexNative::IsSupportedEvent(const PipelineEventPtr& e) const {
    return e.Is<LogEvent>();
}

// 函数说明：处理日志事件，将日志内容按照正则表达式进行分割，并生成新的日志事件
void ProcessorSplitRegexNative::ProcessEvent(PipelineEventGroup& logGroup,
                                             const StringView& logPath,
                                             const PipelineEventPtr& e,
                                             EventsContainer& newEvents) {
    if (!IsSupportedEvent(e)) {
        newEvents.emplace_back(e);
        return;
    }
    // 将事件转换为日志事件
    const LogEvent& sourceEvent = e.Cast<LogEvent>();
    if (!sourceEvent.HasContent(mSourceKey)) {
        newEvents.emplace_back(e);
        return;
    }
    // 获取日志事件中的内容
    StringView sourceVal = sourceEvent.GetContent(mSourceKey);
    std::cout << sourceVal << std::endl;
    std::vector<StringView> logIndex; // 所有分割的日志
    std::vector<StringView> discardIndex; // 用于发送警告的日志
    int feedLines = 0;
    // 对日志内容进行分割
    bool splitSuccess = LogSplit(sourceVal.data(), sourceVal.size(), feedLines, logIndex, discardIndex, logPath);
    *mFeedLines += feedLines;

    // 如果日志解析警告有效并且低级别警告有效
    if (AppConfig::GetInstance()->IsLogParseAlarmValid() && LogtailAlarm::GetInstance()->IsLowLevelAlarmValid()) {
        // 如果分割失败，发送警告
        if (!splitSuccess) {
            GetContext().GetAlarm().SendAlarm(SPLIT_LOG_FAIL_ALARM,
                                              "split log lines fail, please check log_begin_regex, file:"
                                                  + logPath.to_string()
                                                  + ", logs:" + sourceVal.substr(0, 1024).to_string(),
                                              GetContext().GetProjectName(),
                                              GetContext().GetLogstoreName(),
                                              GetContext().GetRegion());
            LOG_ERROR(GetContext().GetLogger(),
                      ("split log lines fail", "please check log_begin_regex")("file_name", logPath)(
                          "log bytes", sourceVal.size() + 1)("first 1KB log", sourceVal.substr(0, 1024).to_string()));
        }
        // 如果有丢失的数据，发送警告
        for (auto& discardData : discardIndex) {
            GetContext().GetAlarm().SendAlarm(SPLIT_LOG_FAIL_ALARM,
                                              "split log lines discard data, file:" + logPath.to_string()
                                                  + ", logs:" + discardData.substr(0, 1024).to_string(),
                                              GetContext().GetProjectName(),
                                              GetContext().GetLogstoreName(),
                                              GetContext().GetRegion());
            LOG_WARNING(
                GetContext().GetLogger(),
                ("split log lines discard data", "please check log_begin_regex")("file_name", logPath)(
                    "log bytes", sourceVal.size() + 1)("first 1KB log", discardData.substr(0, 1024).to_string()));
        }
    }
    // 如果没有分割的日志，直接返回
    if (logIndex.size() == 0) {
        return;
    }
    long sourceoffset = 0L;
    // 如果日志事件中有文件偏移量，获取文件偏移量
    if (sourceEvent.HasContent(LOG_RESERVED_KEY_FILE_OFFSET)) {
        sourceoffset = atol(sourceEvent.GetContent(LOG_RESERVED_KEY_FILE_OFFSET).data()); // 使用更安全的方法
    }
    // 复制源键
    StringBuffer splitKey = logGroup.GetSourceBuffer()->CopyString(mSourceKey);
    // 遍历所有分割的日志
    for (auto& content : logIndex) {
        // 创建新的日志事件
        std::unique_ptr<LogEvent> targetEvent = LogEvent::CreateEvent(logGroup.GetSourceBuffer());
        // 设置新日志事件的时间戳
        targetEvent->SetTimestamp(sourceEvent.GetTimestamp(),
                                  sourceEvent.GetTimestampNanosecond()); // 容易忘记其他字段，有更好的解决方案吗？
        // 设置新日志事件的内容
        targetEvent->SetContentNoCopy(StringView(splitKey.data, splitKey.size), content);
        // 如果需要添加日志位置元数据
        if (mAppendingLogPositionMeta) {
            // 计算偏移量
            auto const offset = sourceoffset + (content.data() - sourceVal.data());
            // 复制偏移量字符串
            StringBuffer offsetStr = logGroup.GetSourceBuffer()->CopyString(std::to_string(offset));
            // 设置新日志事件的文件偏移量
            targetEvent->SetContentNoCopy(LOG_RESERVED_KEY_FILE_OFFSET, StringView(offsetStr.data, offsetStr.size));
        }
        // 如果源日志事件的内容数量大于1，复制其他字段
        if (sourceEvent.GetContents().size() > 1) {
            for (auto& kv : sourceEvent.GetContents()) {
                if (kv.first != mSourceKey && kv.first != LOG_RESERVED_KEY_FILE_OFFSET) {
                    targetEvent->SetContentNoCopy(kv.first, kv.second);
                }
            }
        }
        // 添加新的日志事件
        newEvents.emplace_back(std::move(targetEvent));
    }
}

// ProcessorSplitRegexNative::LogSplit函数
// 参数：
// buffer：输入的字符数组
// size：字符数组的大小
// lineFeed：换行符的数量
// logIndex：存储日志索引的向量
// discardIndex：存储丢弃索引的向量
// logPath：日志路径
// 返回值：布尔值，表示是否成功
bool ProcessorSplitRegexNative::LogSplit(const char* buffer,
                                         int32_t size,
                                         int32_t& lineFeed,
                                         std::vector<StringView>& logIndex,
                                         std::vector<StringView>& discardIndex,
                                         const StringView& logPath) {
    /*
        multiBeginIndex：用于缓存当前解析日志。开始下一个日志时清除。
        begIndex：当前行的开始索引
        endIndex：当前行的结束索引

        支持的正则表达式组合：
        1. 开始
        2. 开始 + 继续
        3. 开始 + 结束
        4. 继续 + 结束
        5. 结束
    */
    int multiBeginIndex = 0; // 初始化multiBeginIndex为0
    int begIndex = 0; // 初始化begIndex为0
    int endIndex = 0; // 初始化endIndex为0
    bool anyMatched = false; // 初始化anyMatched为false
    lineFeed = 0; // 初始化lineFeed为0
    std::string exception; // 初始化exception为空字符串
    SplitState state = SPLIT_UNMATCH; // 初始化state为SPLIT_UNMATCH
    while (endIndex <= size) { // 当endIndex小于等于size时，进入循环
        if (endIndex == size || buffer[endIndex] == '\n') { // 如果endIndex等于size或者buffer[endIndex]是换行符
            lineFeed++; // lineFeed加1
            exception.clear(); // 清除exception
            // 状态机有三种状态（SPLIT_UNMATCH，SPLIT_BEGIN，SPLIT_CONTINUE）
            switch (state) {
                case SPLIT_UNMATCH: // 当state为SPLIT_UNMATCH时
                    if (!mMultiline.IsMultiline()) { // 如果不是多行日志
                        anyMatched = true; // 设置anyMatched为true
                        logIndex.emplace_back(buffer + begIndex, endIndex - begIndex); // 在logIndex后面添加一个元素
                        multiBeginIndex = endIndex + 1; // 设置multiBeginIndex为endIndex + 1
                        break;
                    } else if (mMultiline.GetStartPatternReg() != nullptr) { // 如果开始模式正则表达式不为空
                        if (BoostRegexMatch(
                                buffer + begIndex, endIndex - begIndex, *mMultiline.GetStartPatternReg(), exception)) {
                            // 如果匹配成功
                            // 清除旧缓存，将当前行作为新缓存
                            if (multiBeginIndex != begIndex) { // 如果multiBeginIndex不等于begIndex
                                anyMatched = true; // 设置anyMatched为true
                                logIndex[logIndex.size() - 1] = StringView(logIndex[logIndex.size() - 1].begin(),
                                                                           logIndex[logIndex.size() - 1].length()
                                                                               + begIndex - 1 - multiBeginIndex);
                                // 更新logIndex的最后一个元素
                                multiBeginIndex = begIndex; // 设置multiBeginIndex为begIndex
                            }
                            state = SPLIT_BEGIN; // 设置state为SPLIT_BEGIN
                            break;
                        }
                        HandleUnmatchLogs(buffer, multiBeginIndex, endIndex, logIndex, discardIndex);
                        // 处理不匹配的日志
                        break;
                    }
                    // ContinuePatternReg可以匹配0次或多次，如果不匹配，继续尝试EndPatternReg
                    if (mMultiline.GetContinuePatternReg() != nullptr
                        && BoostRegexMatch(
                            buffer + begIndex, endIndex - begIndex, *mMultiline.GetContinuePatternReg(), exception)) {
                        state = SPLIT_CONTINUE; // 如果匹配成功，状态变为SPLIT_CONTINUE
                        break;
                    }
                    if (mMultiline.GetEndPatternReg() != nullptr
                        && BoostRegexMatch(
                            buffer + begIndex, endIndex - begIndex, *mMultiline.GetEndPatternReg(), exception)) {
                        // 输出缓存中从multiBeginIndex到endIndex的日志
                        anyMatched = true; // 匹配成功，设置anyMatched为true
                        logIndex.emplace_back(buffer + multiBeginIndex,
                                              endIndex - multiBeginIndex); // 在logIndex后面添加一个元素
                        multiBeginIndex = endIndex + 1; // 更新multiBeginIndex为endIndex + 1
                        break;
                    }
                    HandleUnmatchLogs(buffer, multiBeginIndex, endIndex, logIndex, discardIndex); // 处理不匹配的日志
                    break;

                case SPLIT_BEGIN: // 当状态为SPLIT_BEGIN时
                    // ContinuePatternReg可以匹配0次或多次，如果不匹配，继续尝试其他的
                    if (mMultiline.GetContinuePatternReg() != nullptr
                        && BoostRegexMatch(
                            buffer + begIndex, endIndex - begIndex, *mMultiline.GetContinuePatternReg(), exception)) {
                        state = SPLIT_CONTINUE; // 如果匹配成功，状态变为SPLIT_CONTINUE
                        break;
                    }
                    if (mMultiline.GetEndPatternReg() != nullptr) {
                        if (BoostRegexMatch(
                                buffer + begIndex, endIndex - begIndex, *mMultiline.GetEndPatternReg(), exception)) {
                            anyMatched = true; // 匹配成功，设置anyMatched为true
                            logIndex.emplace_back(buffer + multiBeginIndex,
                                                  endIndex - multiBeginIndex); // 在logIndex后面添加一个元素
                            multiBeginIndex = endIndex + 1; // 更新multiBeginIndex为endIndex + 1
                            state = SPLIT_UNMATCH; // 状态变为SPLIT_UNMATCH
                        }
                        // 对于情况：开始不匹配结束
                        // 所以即使不匹配LogEngReg，日志也不能被处理为不匹配
                    } else if (mMultiline.GetStartPatternReg() != nullptr) {
                        anyMatched = true; // 设置anyMatched为true
                        if (BoostRegexMatch(
                                buffer + begIndex, endIndex - begIndex, *mMultiline.GetStartPatternReg(), exception)) {
                            if (multiBeginIndex != begIndex) { // 如果multiBeginIndex不等于begIndex
                                logIndex.emplace_back(buffer + multiBeginIndex,
                                                      begIndex - 1 - multiBeginIndex); // 在logIndex后面添加一个元素
                                multiBeginIndex = begIndex; // 更新multiBeginIndex为begIndex
                            }
                        } else if (mMultiline.GetContinuePatternReg() != nullptr) {
                            // 情况：开始+继续，但我们在这里遇到不匹配的日志
                            logIndex.emplace_back(buffer + multiBeginIndex,
                                                  begIndex - 1 - multiBeginIndex); // 在logIndex后面添加一个元素
                            multiBeginIndex = begIndex; // 更新multiBeginIndex为begIndex
                            HandleUnmatchLogs(
                                buffer, multiBeginIndex, endIndex, logIndex, discardIndex); // 处理不匹配的日志
                            state = SPLIT_UNMATCH; // 状态变为SPLIT_UNMATCH
                        }
                        // 否则的情况：开始+结束或开始，我们应该保留不匹配的日志在缓存中
                    }
                    break;
                case SPLIT_CONTINUE: // 当状态为SPLIT_CONTINUE时
                    // ContinuePatternReg可以匹配0次或多次，如果不匹配，继续尝试其他的
                    if (mMultiline.GetContinuePatternReg() != nullptr
                        && BoostRegexMatch(
                            buffer + begIndex, endIndex - begIndex, *mMultiline.GetContinuePatternReg(), exception)) {
                        break; // 如果匹配成功，跳出当前循环
                    }
                    if (mMultiline.GetEndPatternReg() != nullptr) { // 如果结束模式正则表达式不为空
                        if (BoostRegexMatch(
                                buffer + begIndex, endIndex - begIndex, *mMultiline.GetEndPatternReg(), exception)) {
                            // 如果匹配成功
                            anyMatched = true; // 设置anyMatched为true
                            logIndex.emplace_back(buffer + multiBeginIndex,
                                                  endIndex - multiBeginIndex); // 在logIndex后面添加一个元素
                            multiBeginIndex = endIndex + 1; // 更新multiBeginIndex为endIndex + 1
                            state = SPLIT_UNMATCH; // 状态变为SPLIT_UNMATCH
                        } else {
                            HandleUnmatchLogs(
                                buffer, multiBeginIndex, endIndex, logIndex, discardIndex); // 处理不匹配的日志
                            state = SPLIT_UNMATCH; // 状态变为SPLIT_UNMATCH
                        }
                    } else if (mMultiline.GetStartPatternReg() != nullptr) { // 如果开始模式正则表达式不为空
                        if (BoostRegexMatch(
                                buffer + begIndex, endIndex - begIndex, *mMultiline.GetStartPatternReg(), exception)) {
                            // 如果匹配成功
                            anyMatched = true; // 设置anyMatched为true
                            logIndex.emplace_back(buffer + multiBeginIndex,
                                                  begIndex - 1 - multiBeginIndex); // 在logIndex后面添加一个元素
                            multiBeginIndex = begIndex; // 更新multiBeginIndex为begIndex
                            state = SPLIT_BEGIN; // 状态变为SPLIT_BEGIN
                        } else {
                            anyMatched = true; // 设置anyMatched为true
                            logIndex.emplace_back(buffer + multiBeginIndex,
                                                  begIndex - 1 - multiBeginIndex); // 在logIndex后面添加一个元素
                            multiBeginIndex = begIndex; // 更新multiBeginIndex为begIndex
                            HandleUnmatchLogs(
                                buffer, multiBeginIndex, endIndex, logIndex, discardIndex); // 处理不匹配的日志
                            state = SPLIT_UNMATCH; // 状态变为SPLIT_UNMATCH
                        }
                    } else {
                        anyMatched = true; // 设置anyMatched为true
                        logIndex.emplace_back(buffer + multiBeginIndex,
                                              begIndex - 1 - multiBeginIndex); // 在logIndex后面添加一个元素
                        multiBeginIndex = begIndex; // 更新multiBeginIndex为begIndex
                        HandleUnmatchLogs(
                            buffer, multiBeginIndex, endIndex, logIndex, discardIndex); // 处理不匹配的日志
                        state = SPLIT_UNMATCH; // 状态变为SPLIT_UNMATCH
                    }
                    break; // 跳出当前循环
            }
            begIndex = endIndex + 1; // 更新begIndex为endIndex + 1
            if (!exception.empty()) { // 如果异常信息不为空
                if (AppConfig::GetInstance()->IsLogParseAlarmValid()) { // 如果日志解析警报有效
                    if (GetContext().GetAlarm().IsLowLevelAlarmValid()) { // 如果低级别警报有效
                        // 记录错误日志，包括异常信息，项目名，日志存储名和文件路径
                        LOG_ERROR(GetContext().GetLogger(),
                                  ("regex_match in LogSplit fail, exception", exception)("project",
                                                                                         GetContext().GetProjectName())(
                                      "logstore", GetContext().GetLogstoreName())("file", logPath));
                    }
                    // 发送警报，包括警报类型，警报信息，项目名，日志存储名和区域
                    GetContext().GetAlarm().SendAlarm(REGEX_MATCH_ALARM,
                                                      "regex_match in LogSplit fail:" + exception + ", file"
                                                          + logPath.to_string(),
                                                      GetContext().GetProjectName(),
                                                      GetContext().GetLogstoreName(),
                                                      GetContext().GetRegion());
                }
            }
        }
        endIndex++; // endIndex自增1
    }
    // 如果multiBeginIndex小于size，我们应该清除从`multiBeginIndex`到`size`的日志。
    if (multiBeginIndex < size) {
        if (!mMultiline.IsMultiline()) { // 如果不是多行模式
            // 在logIndex后面添加一个元素，内容是buffer中从multiBeginIndex开始，长度为size - multiBeginIndex的部分
            logIndex.emplace_back(buffer + multiBeginIndex, size - multiBeginIndex);
        } else { // 如果是多行模式
            // 如果buffer的最后一个字符是'\n'，则endIndex为size - 1，否则为size
            endIndex = buffer[size - 1] == '\n' ? size - 1 : size;
            if (mMultiline.GetStartPatternReg() != NULL
                && mMultiline.GetEndPatternReg() == NULL) { // 如果开始模式正则表达式不为空且结束模式正则表达式为空
                anyMatched = true; // 设置anyMatched为true
                // 如果日志不匹配，它们已经被立即处理。所以这里的日志必须是匹配的。
                // 在logIndex后面添加一个元素，内容是buffer中从multiBeginIndex开始，长度为endIndex -
                // multiBeginIndex的部分
                logIndex.emplace_back(buffer + multiBeginIndex, endIndex - multiBeginIndex);
            } else if (mMultiline.GetStartPatternReg() == NULL && mMultiline.GetContinuePatternReg() == NULL
                       && mMultiline.GetEndPatternReg()
                           != NULL) { // 如果开始模式正则表达式为空，继续模式正则表达式为空，且结束模式正则表达式不为空
                // 如果缓存中还有日志，那就意味着没有结束行。我们可以将它们处理为不匹配的。
                if (mMultiline.mUnmatchedContentTreatment
                    == MultilineOptions::UnmatchedContentTreatment::DISCARD) { // 如果不匹配内容的处理方式为丢弃
                    for (int i = multiBeginIndex; i <= endIndex; i++) { // 从multiBeginIndex到endIndex遍历
                        if (i == endIndex || buffer[i] == '\n') { // 如果i等于endIndex或buffer[i]等于'\n'
                            // 在discardIndex后面添加一个元素，内容是buffer中从multiBeginIndex开始，长度为i -
                            // multiBeginIndex的部分
                            discardIndex.emplace_back(buffer + multiBeginIndex, i - multiBeginIndex);
                            multiBeginIndex = i + 1; // 更新multiBeginIndex为i + 1
                        }
                    }
                } else if (mMultiline.mUnmatchedContentTreatment
                           == MultilineOptions::UnmatchedContentTreatment::
                               SINGLE_LINE) { // 如果不匹配内容的处理方式为单行
                    for (int i = multiBeginIndex; i <= endIndex; i++) { // 从multiBeginIndex到endIndex遍历
                        if (i == endIndex || buffer[i] == '\n') { // 如果i等于endIndex或buffer[i]等于'\n'
                            // 在logIndex后面添加一个元素，内容是buffer中从multiBeginIndex开始，长度为i -
                            // multiBeginIndex的部分
                            logIndex.emplace_back(buffer + multiBeginIndex, i - multiBeginIndex);
                            multiBeginIndex = i + 1; // 更新multiBeginIndex为i + 1
                        }
                    }
                }
            } else { // 其他情况
                // 处理不匹配的日志
                HandleUnmatchLogs(buffer, multiBeginIndex, endIndex, logIndex, discardIndex);
            }
        }
    }
    return anyMatched; // 返回anyMatched}
}

// ProcessorSplitRegexNative类的成员函数，处理未匹配的日志
void ProcessorSplitRegexNative::HandleUnmatchLogs(const char* buffer, // 输入的字符缓冲区
                                                  int& multiBeginIndex, // 多行日志的开始索引
                                                  int endIndex, // 结束索引
                                                  std::vector<StringView>& logIndex, // 日志索引
                                                  std::vector<StringView>& discardIndex) { // 废弃索引
    // 如果只有结束模式正则表达式，无法确定日志在哪里不匹配
    if (mMultiline.GetStartPatternReg() == nullptr && mMultiline.GetContinuePatternReg() == nullptr
        && mMultiline.GetEndPatternReg() != nullptr) {
        return; // 直接返回，不进行处理
    }
    // 如果未匹配内容的处理方式是丢弃
    if (mMultiline.mUnmatchedContentTreatment == MultilineOptions::UnmatchedContentTreatment::DISCARD) {
        // 遍历从开始索引到结束索引的字符
        for (int i = multiBeginIndex; i <= endIndex; i++) {
            // 如果到达结束索引或者遇到换行符
            if (i == endIndex || buffer[i] == '\n') {
                // 在废弃索引中添加一个新的元素，元素的内容是从开始索引到当前索引的字符
                discardIndex.emplace_back(buffer + multiBeginIndex, i - multiBeginIndex);
                // 更新开始索引为当前索引+1
                multiBeginIndex = i + 1;
            }
        }
    }
    // 如果未匹配内容的处理方式是单行
    else if (mMultiline.mUnmatchedContentTreatment == MultilineOptions::UnmatchedContentTreatment::SINGLE_LINE) {
        // 遍历从开始索引到结束索引的字符
        for (int i = multiBeginIndex; i <= endIndex; i++) {
            // 如果到达结束索引或者遇到换行符
            if (i == endIndex || buffer[i] == '\n') {
                // 在日志索引中添加一个新的元素，元素的内容是从开始索引到当前索引的字符
                logIndex.emplace_back(buffer + multiBeginIndex, i - multiBeginIndex);
                // 更新开始索引为当前索引+1
                multiBeginIndex = i + 1;
            }
        }
    }
}

} // namespace logtail
