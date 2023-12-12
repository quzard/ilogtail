// Copyright 2021 iLogtail Authors
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

package stdout

import (
	"bytes"
	"errors"
	"regexp"
	"strings"
	"time"

	"github.com/alibaba/ilogtail/pkg/logger"
	"github.com/alibaba/ilogtail/pkg/pipeline"
	"github.com/alibaba/ilogtail/pkg/protocol"
	"github.com/alibaba/ilogtail/pkg/util"
)

// 定义一些全局变量
var (
	delimiter         = []byte{' '}  // 分隔符
	contianerdFullTag = []byte{'F'}  // 容器全标签
	contianerdPartTag = []byte{'P'}  // 容器部分标签
	lineSuffix        = []byte{'\n'} // 行后缀
)

// DockerJSONLog 结构体，用于解析 Docker 的 JSON 日志
type DockerJSONLog struct {
	LogContent string `json:"log"`    // 日志内容
	StreamType string `json:"stream"` // 流类型
	Time       string `json:"time"`   // 时间
}

// LogMessage 结构体，用于存储日志消息
type LogMessage struct {
	Time       string // 时间
	StreamType string // 流类型
	Content    []byte // 内容
	Safe       bool   // 是否安全
}

// safeContent 方法，为内容分配自己的内存，以避免修改
func (l *LogMessage) safeContent() {
	if !l.Safe { // 如果不安全
		b := make([]byte, len(l.Content)) // 创建一个新的字节切片
		copy(b, l.Content)                // 将内容复制到新的字节切片
		l.Content = b                     // 将新的字节切片赋值给内容
		l.Safe = true                     // 设置为安全
	}
}

// DockerStdoutProcessor 结构体，用于处理 Docker 的标准输出
type DockerStdoutProcessor struct {
	beginLineReg         *regexp.Regexp     // 开始行正则表达式
	beginLineTimeout     time.Duration      // 开始行超时
	beginLineCheckLength int                // 开始行检查长度
	maxLogSize           int                // 最大日志大小
	stdout               bool               // 是否标准输出
	stderr               bool               // 是否标准错误
	context              pipeline.Context   // 管道上下文
	collector            pipeline.Collector // 管道收集器

	needCheckStream bool                   // 是否需要检查流
	source          string                 // 源
	tags            []protocol.Log_Content // 标签
	fieldNum        int                    // 字段数量

	// 保存最后解析的日志
	lastLogs      []*LogMessage // 最后的日志
	lastLogsCount int           // 最后的日志数量
}

// NewDockerStdoutProcessor 函数，用于创建一个新的 DockerStdoutProcessor
func NewDockerStdoutProcessor(beginLineReg *regexp.Regexp, beginLineTimeout time.Duration, beginLineCheckLength int,
	maxLogSize int, stdout bool, stderr bool, context pipeline.Context, collector pipeline.Collector,
	tags map[string]string, source string) *DockerStdoutProcessor {
	// 创建一个新的 DockerStdoutProcessor
	processor := &DockerStdoutProcessor{
		beginLineReg:         beginLineReg,
		beginLineTimeout:     beginLineTimeout,
		beginLineCheckLength: beginLineCheckLength,
		maxLogSize:           maxLogSize,
		stdout:               stdout,
		stderr:               stderr,
		context:              context,
		collector:            collector,
		source:               source,
	}

	// 如果 stdout 和 stderr 都为 true，则不需要检查流，否则需要检查流
	if stdout && stderr {
		processor.needCheckStream = false
	} else {
		processor.needCheckStream = true
	}
	// 遍历标签，将其添加到 processor 的标签中
	for k, v := range tags {
		processor.tags = append(processor.tags, protocol.Log_Content{Key: k, Value: v})
	}
	// 设置字段数量
	processor.fieldNum = len(processor.tags) + 3
	// 返回新创建的 DockerStdoutProcessor
	return processor
}

// parseCRILog 函数用于解析 CRI 日志格式的日志。
// CRI 日志格式示例 :
// 2017-09-12T22:32:21.212861448Z stdout 2017-09-12 22:32:21.212 [INFO][88] table.go 710: Invalidating dataplane cache
func parseCRILog(line []byte) (*LogMessage, error) {
	// 参考：https://github.com/kubernetes/kubernetes/blob/master/pkg/kubelet/kuberuntime/logs/logs.go#L125-L169
	log := &LogMessage{}                // 创建一个新的 LogMessage 对象
	idx := bytes.Index(line, delimiter) // 在 line 中查找 delimiter 的位置
	if idx < 0 {                        // 如果找不到 delimiter
		return &LogMessage{ // 返回一个新的 LogMessage 对象，内容为 line
			Content: line,
		}, errors.New("invalid CRI log, timestamp not found") // 返回错误，表示找不到时间戳
	}
	log.Time = string(line[:idx]) // 将 line 中 delimiter 之前的部分作为时间

	temp := line[idx+1:]               // 获取 delimiter 之后的部分
	idx = bytes.Index(temp, delimiter) // 在 temp 中查找 delimiter 的位置
	if idx < 0 {                       // 如果找不到 delimiter
		return &LogMessage{ // 返回一个新的 LogMessage 对象，内容为 line
			Content: line,
		}, errors.New("invalid CRI log, stream type not found") // 返回错误，表示找不到流类型
	}
	log.StreamType = string(temp[:idx]) // 将 temp 中 delimiter 之前的部分作为流类型

	temp = temp[idx+1:] // 获取 delimiter 之后的部分

	switch {
	case bytes.HasPrefix(temp, contianerdFullTag): // 如果 temp 以 contianerdFullTag 开头
		i := bytes.Index(temp, delimiter) // 在 temp 中查找 delimiter 的位置
		if i < 0 {                        // 如果找不到 delimiter
			return &LogMessage{ // 返回一个新的 LogMessage 对象，内容为 line
				Content: line,
			}, errors.New("invalid CRI log, log content not found") // 返回错误，表示找不到日志内容
		}
		log.Content = temp[i+1:] // 将 temp 中 delimiter 之后的部分作为日志内容
	case bytes.HasPrefix(temp, contianerdPartTag): // 如果 temp 以 contianerdPartTag 开头
		i := bytes.Index(temp, delimiter) // 在 temp 中查找 delimiter 的位置
		if i < 0 {                        // 如果找不到 delimiter
			return &LogMessage{ // 返回一个新的 LogMessage 对象，内容为 line
				Content: line,
			}, errors.New("invalid CRI log, log content not found") // 返回错误，表示找不到日志内容
		}
		if bytes.HasSuffix(temp, lineSuffix) { // 如果 temp 以 lineSuffix 结尾
			log.Content = temp[i+1 : len(temp)-1] // 将 temp 中 delimiter 之后的部分（不包括 lineSuffix）作为日志内容
		} else {
			log.Content = temp[i+1:] // 将 temp 中 delimiter 之后的部分作为日志内容
		}
	default: // 如果 temp 既不以 contianerdFullTag 开头，也不以 contianerdPartTag 开头
		log.Content = temp // 将 temp 作为日志内容
	}
	return log, nil // 返回 log 和 nil 错误
}

// parseDockerJSONLog 函数用于解析 Docker JSON 日志格式的日志。
// Docker JSON 日志格式示例:
// {"log":"1:M 09 Nov 13:27:36.276 # User requested shutdown...\n","stream":"stdout", "time":"2018-05-16T06:28:41.2195434Z"}
func parseDockerJSONLog(line []byte) (*LogMessage, error) {
	dockerLog := &DockerJSONLog{} // 创建一个新的 DockerJSONLog 对象
	// 尝试将 line 解析为 JSON 并存入 dockerLog
	if err := dockerLog.UnmarshalJSON(line); err != nil {
		lm := new(LogMessage) // 创建一个新的 LogMessage 对象
		lm.Content = line     // 将 line 作为内容
		return lm, err        // 返回 LogMessage 对象和错误
	}
	// 创建一个新的 LogMessage 对象，将 dockerLog 的各个字段赋值给对应的字段
	l := &LogMessage{
		Time:       dockerLog.Time,
		StreamType: dockerLog.StreamType,
		Content:    util.ZeroCopyStringToBytes(dockerLog.LogContent),
		Safe:       true,
	}
	dockerLog.LogContent = "" // 清空 dockerLog 的 LogContent 字段
	return l, nil             // 返回 LogMessage 对象和 nil 错误
}

// ParseContainerLogLine 方法用于解析容器日志行
func (p *DockerStdoutProcessor) ParseContainerLogLine(line []byte) *LogMessage {
	if len(line) == 0 { // 如果 line 为空
		// 记录警告日志
		logger.Warning(p.context.GetRuntimeContext(), "PARSE_DOCKER_LINE_ALARM", "parse docker line error", "empty line")
		return &LogMessage{} // 返回一个新的 LogMessage 对象
	}
	if line[0] == '{' { // 如果 line 的第一个字符是 '{'
		// 尝试解析 Docker JSON 日志格式的日志
		log, err := parseDockerJSONLog(line)
		if err != nil { // 如果解析出错
			// 记录警告日志
			logger.Warning(p.context.GetRuntimeContext(), "PARSE_DOCKER_LINE_ALARM", "parse json docker line error", err.Error(), "line", util.CutString(string(line), 512))
		}
		return log // 返回解析得到的 LogMessage 对象
	}
	// 尝试解析 CRI 日志格式的日志
	log, err := parseCRILog(line)
	if err != nil { // 如果解析出错
		// 记录警告日志
		logger.Warning(p.context.GetRuntimeContext(), "PARSE_DOCKER_LINE_ALARM", "parse cri docker line error", err.Error(), "line", util.CutString(string(line), 512))
	}
	return log // 返回解析得到的 LogMessage 对象
}

// StreamAllowed 方法用于检查是否允许处理给定的日志流
func (p *DockerStdoutProcessor) StreamAllowed(log *LogMessage) bool {
	if p.needCheckStream { // 如果需要检查流
		if len(log.StreamType) == 0 { // 如果流类型为空
			return true // 允许处理
		}
		if p.stderr { // 如果允许处理标准错误流
			return log.StreamType == "stderr" // 如果流类型是 "stderr"，则允许处理
		}
		return log.StreamType == "stdout" // 如果流类型是 "stdout"，则允许处理
	}
	return true // 如果不需要检查流，直接允许处理
}

// Process 函数处理 Docker 的标准输出，将其转换为日志消息
func (p *DockerStdoutProcessor) Process(fileBlock []byte, noChangeInterval time.Duration) int {
	nowIndex := 0       // 当前处理的索引位置
	processedCount := 0 // 已处理的字节数
	// 遍历 fileBlock，寻找 '\n'，即日志行的结束位置
	for nextIndex := bytes.IndexByte(fileBlock, '\n'); nextIndex >= 0; nextIndex = bytes.IndexByte(fileBlock[nowIndex:], '\n') {
		nextIndex += nowIndex // 更新 nextIndex 为相对于 fileBlock 的位置
		// 解析当前日志行
		thisLog := p.ParseContainerLogLine(fileBlock[nowIndex : nextIndex+1])
		// 判断当前日志行是否允许输出
		if p.StreamAllowed(thisLog) {
			// 获取日志行的最后一个字符
			lastChar := uint8('\n')
			if contentLen := len(thisLog.Content); contentLen > 0 {
				lastChar = thisLog.Content[contentLen-1]
			}
			switch {
			case p.beginLineReg == nil && len(p.lastLogs) == 0 && lastChar == '\n':
				// 收集单行日志
				p.collector.AddRawLogWithContext(p.newRawLogBySingleLine(thisLog), map[string]interface{}{"source": p.source})
			case p.beginLineReg == nil:
				// 收集被分割的多行日志，例如 containerd 的日志
				if lastChar != '\n' {
					thisLog.safeContent()
				}
				p.lastLogs = append(p.lastLogs, thisLog)
				p.lastLogsCount += len(thisLog.Content) + 24
				if lastChar == '\n' {
					p.collector.AddRawLogWithContext(p.newRawLogByMultiLine(), map[string]interface{}{"source": p.source})
				}
			default:
				// 收集用户的多行日志
				var checkLine []byte
				if len(thisLog.Content) > p.beginLineCheckLength {
					checkLine = thisLog.Content[0:p.beginLineCheckLength]
				} else {
					checkLine = thisLog.Content
				}
				if p.beginLineReg.Match(checkLine) {
					if len(p.lastLogs) != 0 {
						p.collector.AddRawLogWithContext(p.newRawLogByMultiLine(), map[string]interface{}{"source": p.source})
					}
				}
				thisLog.safeContent()
				p.lastLogs = append(p.lastLogs, thisLog)
				p.lastLogsCount += len(thisLog.Content) + 24
			}
		}

		// 当解析到一个以 '\n' 结束的行时，总是设置 processedCount
		// 如果不这样做，处理时间复杂度将是 o(n^2)
		processedCount = nextIndex + 1
		nowIndex = nextIndex + 1
	}

	// 最后一行和多行超时
	if len(p.lastLogs) > 0 && (noChangeInterval > p.beginLineTimeout || p.lastLogsCount > p.maxLogSize) {
		p.collector.AddRawLogWithContext(p.newRawLogByMultiLine(), map[string]interface{}{"source": p.source})
	}

	// 没有新行
	if nowIndex == 0 && len(fileBlock) > 0 {
		l := &LogMessage{Time: "_time_", StreamType: "_source_", Content: fileBlock}
		p.collector.AddRawLogWithContext(p.newRawLogBySingleLine(l), map[string]interface{}{"source": p.source})
		processedCount = len(fileBlock)
	}
	return processedCount
}

// newRawLogBySingleLine 函数将单行日志转换为 protocol.Log
func (p *DockerStdoutProcessor) newRawLogBySingleLine(msg *LogMessage) *protocol.Log {
	nowTime := time.Now() // 获取当前时间
	log := &protocol.Log{
		Contents: make([]*protocol.Log_Content, 0, p.fieldNum), // 初始化 Log 的 Contents
	}
	protocol.SetLogTimeWithNano(log, uint32(nowTime.Unix()), uint32(nowTime.Nanosecond())) // 设置 Log 的时间
	if len(msg.Content) > 0 && msg.Content[len(msg.Content)-1] == '\n' {
		msg.Content = msg.Content[0 : len(msg.Content)-1] // 去掉内容末尾的 '\n'
	}
	msg.safeContent() // 安全处理内容
	// 添加内容到 Log
	log.Contents = append(log.Contents, &protocol.Log_Content{
		Key:   "content",
		Value: util.ZeroCopyBytesToString(msg.Content),
	})
	// 添加时间到 Log
	log.Contents = append(log.Contents, &protocol.Log_Content{
		Key:   "_time_",
		Value: msg.Time,
	})
	// 添加源到 Log
	log.Contents = append(log.Contents, &protocol.Log_Content{
		Key:   "_source_",
		Value: msg.StreamType,
	})
	// 添加标签到 Log
	for i := range p.tags {
		copy := p.tags[i]
		log.Contents = append(log.Contents, &copy)
	}
	return log
}

// newRawLogByMultiLine 函数将多行日志转换为 protocol.Log
func (p *DockerStdoutProcessor) newRawLogByMultiLine() *protocol.Log {
	lastOne := p.lastLogs[len(p.lastLogs)-1] // 获取最后一条日志
	if len(lastOne.Content) > 0 && lastOne.Content[len(lastOne.Content)-1] == '\n' {
		lastOne.Content = lastOne.Content[:len(lastOne.Content)-1] // 去掉内容末尾的 '\n'
	}
	var multiLine strings.Builder // 初始化一个字符串构建器
	var sum int
	for _, log := range p.lastLogs {
		sum += len(log.Content) // 计算所有日志内容的长度
	}
	multiLine.Grow(sum) // 设置字符串构建器的容量
	for index, log := range p.lastLogs {
		multiLine.Write(log.Content) // 将日志内容写入字符串构建器
		// @note 强制设置 lastLog 的内容为 nil，让 GC 回收这些日志
		p.lastLogs[index] = nil
	}

	nowTime := time.Now() // 获取当前时间
	log := &protocol.Log{
		Contents: make([]*protocol.Log_Content, 0, p.fieldNum), // 初始化 Log 的 Contents
	}
	protocol.SetLogTimeWithNano(log, uint32(nowTime.Unix()), uint32(nowTime.Nanosecond())) // 设置 Log 的时间
	// 添加内容到 Log
	log.Contents = append(log.Contents, &protocol.Log_Content{
		Key:   "content",
		Value: multiLine.String(),
	})
	// 添加时间到 Log
	log.Contents = append(log.Contents, &protocol.Log_Content{
		Key:   "_time_",
		Value: lastOne.Time,
	})
	// 添加源到 Log
	log.Contents = append(log.Contents, &protocol.Log_Content{
		Key:   "_source_",
		Value: lastOne.StreamType,
	})
	// 添加标签到 Log
	for i := range p.tags {
		copy := p.tags[i]
		log.Contents = append(log.Contents, &copy)
	}
	// 重置多行缓存
	p.lastLogs = p.lastLogs[:0]
	p.lastLogsCount = 0
	return log
}
