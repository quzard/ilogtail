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
	"fmt"
	"regexp"
	"sync"
	"time"

	"github.com/docker/docker/api/types"

	"github.com/alibaba/ilogtail/pkg/helper"
	"github.com/alibaba/ilogtail/pkg/logger"
	"github.com/alibaba/ilogtail/pkg/pipeline"
	"github.com/alibaba/ilogtail/pkg/util"
	"github.com/alibaba/ilogtail/plugins/input"
)

const serviceDockerStdoutKey = "service_docker_stdout_v2"

// logDriverSupported函数用于检查容器的日志驱动是否被支持, 只支持json-file
func logDriverSupported(container types.ContainerJSON) bool {
	// 如果容器没有hostConfig，那么默认返回true
	if container.HostConfig == nil {
		return true
	}
	// 根据容器的日志配置类型进行判断
	switch container.HostConfig.LogConfig.Type {
	case "json-file": // 如果是json文件类型，返回true
		return true
	default: // 其他类型返回false
		return false
	}
}

// DockerFileSyner结构体，包含了docker文件的读取器，处理器和信息
type DockerFileSyner struct {
	dockerFileReader    *helper.LogFileReader
	dockerFileProcessor *DockerStdoutProcessor
	info                *helper.DockerInfoDetail
}

// NewDockerFileSynerByFile函数用于通过文件路径创建一个新的DockerFileSyner实例
func NewDockerFileSynerByFile(sds *ServiceDockerStdout, filePath string) *DockerFileSyner {
	dockerInfoDetail := &helper.DockerInfoDetail{}
	dockerInfoDetail.ContainerInfo = types.ContainerJSON{}
	dockerInfoDetail.ContainerInfo.LogPath = filePath
	sds.LogtailInDocker = false
	sds.StartLogMaxOffset = 10 * 1024 * 1024 * 1024
	// 调用NewDockerFileSyner函数创建DockerFileSyner实例
	return NewDockerFileSyner(sds, dockerInfoDetail, sds.checkpointMap)
}

// NewDockerFileSyner函数用于创建一个新的DockerFileSyner实例
func NewDockerFileSyner(sds *ServiceDockerStdout,
	info *helper.DockerInfoDetail,
	checkpointMap map[string]helper.LogFileReaderCheckPoint) *DockerFileSyner {
	var reg *regexp.Regexp
	var err error
	// 如果存在开始行正则表达式，那么编译这个正则表达式
	if len(sds.BeginLineRegex) > 0 {
		if reg, err = regexp.Compile(sds.BeginLineRegex); err != nil {
			// 如果编译失败，记录警告日志
			logger.Warning(sds.context.GetRuntimeContext(), "DOCKER_REGEX_COMPILE_ALARM", "compile begin line regex error, regex", sds.BeginLineRegex, "error", err)
		}
	}

	// 创建新的包ID前缀
	source := util.NewPackIDPrefix(info.ContainerInfo.ID + sds.context.GetConfigName())
	// 获取外部标签
	tags := info.GetExternalTags(sds.ExternalEnvTag, sds.ExternalK8sLabelTag)

	// 创建新的DockerStdoutProcessor实例
	processor := NewDockerStdoutProcessor(reg, time.Duration(sds.BeginLineTimeoutMs)*time.Millisecond, sds.BeginLineCheckLength, sds.MaxLogSize, sds.Stdout, sds.Stderr, sds.context, sds.collector, tags, source)

	// 从checkpointMap中获取checkpoint
	checkpoint, ok := checkpointMap[info.ContainerInfo.ID]
	if !ok {
		// 如果checkpoint不存在，那么根据是否在Docker内部运行Logtail来设置checkpoint的路径
		if sds.LogtailInDocker {
			checkpoint.Path = helper.GetMountedFilePath(info.ContainerInfo.LogPath)
		} else {
			checkpoint.Path = info.ContainerInfo.LogPath
		}

		// 首次监视这个容器
		realPath, stat := helper.TryGetRealPath(checkpoint.Path)
		if realPath == "" {
			// 如果路径不存在，记录警告日志
			logger.Warning(sds.context.GetRuntimeContext(), "DOCKER_STDOUT_STAT_ALARM", "stat log file error, path", checkpoint.Path, "error", "path not found")
		} else {
			// 设置checkpoint的偏移量为文件大小
			checkpoint.Offset = stat.Size()
			if checkpoint.Offset > sds.StartLogMaxOffset {
				// 如果文件过大，记录警告日志，并减小checkpoint的偏移量
				logger.Warning(sds.context.GetRuntimeContext(), "DOCKER_STDOUT_START_ALARM", "log file too big, path", checkpoint.Path, "offset", checkpoint.Offset)
				checkpoint.Offset -= sds.StartLogMaxOffset
			} else {
				checkpoint.Offset = 0
			}
			// 设置checkpoint的状态和路径
			checkpoint.State = helper.GetOSState(stat)
			checkpoint.Path = realPath
		}
	}
	if sds.CloseUnChangedSec < 10 {
		sds.CloseUnChangedSec = 10
	}

	// 记录信息日志
	logger.Info(sds.context.GetRuntimeContext(), "new stdout reader id", info.IDPrefix(),
		"name", info.ContainerInfo.Name, "created", info.ContainerInfo.Created, "status", info.Status(),
		"checkpoint_logpath", checkpoint.Path,
		"in_docker", sds.LogtailInDocker)

	// 创建LogFileReaderConfig实例
	config := helper.LogFileReaderConfig{
		ReadIntervalMs:   sds.ReadIntervalMs,
		MaxReadBlockSize: sds.MaxLogSize,
		CloseFileSec:     sds.CloseUnChangedSec,
		Tracker:          sds.tracker,
	}
	// 创建新的LogFileReader实例
	reader, _ := helper.NewLogFileReader(sds.context.GetRuntimeContext(), checkpoint, config, processor)

	// 返回新的DockerFileSyner实例
	return &DockerFileSyner{
		dockerFileReader:    reader,
		info:                info,
		dockerFileProcessor: processor,
	}
}

type ServiceDockerStdout struct {
	IncludeLabel          map[string]string `comment:"include container label for selector. [Deprecated: use IncludeContainerLabel and IncludeK8sLabel instead]"`
	ExcludeLabel          map[string]string `comment:"exclude container label for selector. [Deprecated: use ExcludeContainerLabel and ExcludeK8sLabel instead]"`
	IncludeEnv            map[string]string `comment:"the container would be selected when it is matched by any environment rules. Furthermore, the regular expression starts with '^' is supported as the env value, such as 'ENVA:^DE.*$'' would hit all containers having any envs starts with DE."`
	ExcludeEnv            map[string]string `comment:"the container would be excluded when it is matched by any environment rules. Furthermore, the regular expression starts with '^' is supported as the env value, such as 'ENVA:^DE.*$'' would hit all containers having any envs starts with DE."`
	IncludeContainerLabel map[string]string `comment:"the container would be selected when it is matched by any container labels. Furthermore, the regular expression starts with '^' is supported as the label value, such as 'LABEL:^DE.*$'' would hit all containers having any labels starts with DE."`
	ExcludeContainerLabel map[string]string `comment:"the container would be excluded when it is matched by any container labels. Furthermore, the regular expression starts with '^' is supported as the label value, such as 'LABEL:^DE.*$'' would hit all containers having any labels starts with DE."`
	IncludeK8sLabel       map[string]string `comment:"the container of pod would be selected when it is matched by any include k8s label rules. Furthermore, the regular expression starts with '^' is supported as the value to match pods."`
	ExcludeK8sLabel       map[string]string `comment:"the container of pod would be excluded when it is matched by any exclude k8s label rules. Furthermore, the regular expression starts with '^' is supported as the value to exclude pods."`
	ExternalEnvTag        map[string]string `comment:"extract the env value as the log tags for one container, such as the value of ENVA would be appended to the 'taga' of log tags when configured 'ENVA:taga' pair."`
	ExternalK8sLabelTag   map[string]string `comment:"extract the pod label value as the log tags for one container, such as the value of LABELA would be appended to the 'taga' of log tags when configured 'LABELA:taga' pair."`
	FlushIntervalMs       int               `comment:"the interval of container discovery, and the timeunit is millisecond. Default value is 3000."`
	ReadIntervalMs        int               `comment:"the interval of read stdout log, and the timeunit is millisecond. Default value is 1000."`
	SaveCheckPointSec     int               `comment:"the interval of save checkpoint, and the timeunit is second. Default value is 60."`
	BeginLineRegex        string            `comment:"the regular expression of begin line for the multi line log."`
	BeginLineTimeoutMs    int               `comment:"the maximum timeout milliseconds for begin line match. Default value is 3000."`
	BeginLineCheckLength  int               `comment:"the prefix length of log line to match the first line. Default value is 10240."`
	MaxLogSize            int               `comment:"the maximum log size. Default value is 512*1024, a.k.a 512K."`
	CloseUnChangedSec     int               `comment:"the reading file would be close when the interval between last read operation is over {CloseUnChangedSec} seconds. Default value is 60."`
	StartLogMaxOffset     int64             `comment:"the first read operation would read {StartLogMaxOffset} size history logs. Default value is 128*1024, a.k.a 128K."`
	Stdout                bool              `comment:"collect stdout log. Default is true."`
	Stderr                bool              `comment:"collect stderr log. Default is true."`
	LogtailInDocker       bool              `comment:"the logtail running mode. Default is true."`
	K8sNamespaceRegex     string            `comment:"the regular expression of kubernetes namespace to match containers."`
	K8sPodRegex           string            `comment:"the regular expression of kubernetes pod to match containers."`
	K8sContainerRegex     string            `comment:"the regular expression of kubernetes container to match containers."`

	// export from ilogtail-trace component
	IncludeLabelRegex map[string]*regexp.Regexp
	ExcludeLabelRegex map[string]*regexp.Regexp
	IncludeEnvRegex   map[string]*regexp.Regexp
	ExcludeEnvRegex   map[string]*regexp.Regexp
	K8sFilter         *helper.K8SFilter

	// for tracker
	tracker           *helper.ReaderMetricTracker
	avgInstanceMetric pipeline.CounterMetric
	addMetric         pipeline.CounterMetric
	deleteMetric      pipeline.CounterMetric

	synerMap      map[string]*DockerFileSyner
	checkpointMap map[string]helper.LogFileReaderCheckPoint
	shutdown      chan struct {
	}
	waitGroup sync.WaitGroup
	context   pipeline.Context
	collector pipeline.Collector

	// Last return of GetAllAcceptedInfoV2
	fullList              map[string]bool
	matchList             map[string]*helper.DockerInfoDetail
	lastUpdateTime        int64
	CollectContainersFlag bool
}

// ServiceDockerStdout 结构体的 Init 方法
func (sds *ServiceDockerStdout) Init(context pipeline.Context) (int, error) {
	sds.context = context                                     // 设置上下文
	helper.ContainerCenterInit()                              // 初始化容器中心
	sds.fullList = make(map[string]bool)                      // 创建 fullList 映射
	sds.matchList = make(map[string]*helper.DockerInfoDetail) // 创建 matchList 映射
	sds.synerMap = make(map[string]*DockerFileSyner)          // 创建 synerMap 映射

	// 设置 MaxLogSize 的上下限
	if sds.MaxLogSize < 1024 {
		sds.MaxLogSize = 1024
	}
	if sds.MaxLogSize > 1024*1024*20 {
		sds.MaxLogSize = 1024 * 1024 * 20
	}
	sds.tracker = helper.NewReaderMetricTracker() // 创建新的 ReaderMetricTracker
	// 注册各种计数器和延迟度量
	sds.context.RegisterCounterMetric(sds.tracker.CloseCounter)
	sds.context.RegisterCounterMetric(sds.tracker.OpenCounter)
	sds.context.RegisterCounterMetric(sds.tracker.ReadSizeCounter)
	sds.context.RegisterCounterMetric(sds.tracker.ReadCounter)
	sds.context.RegisterCounterMetric(sds.tracker.FileSizeCounter)
	sds.context.RegisterCounterMetric(sds.tracker.FileRotatorCounter)
	sds.context.RegisterLatencyMetric(sds.tracker.ProcessLatency)

	// 创建新的平均度量和计数器度量
	sds.avgInstanceMetric = helper.NewAverageMetric("container_count")
	sds.addMetric = helper.NewCounterMetric("add_container")
	sds.deleteMetric = helper.NewCounterMetric("remove_container")
	// 注册这些度量
	sds.context.RegisterCounterMetric(sds.avgInstanceMetric)
	sds.context.RegisterCounterMetric(sds.addMetric)
	sds.context.RegisterCounterMetric(sds.deleteMetric)

	var err error
	// 分割并从映射中获取正则表达式
	sds.IncludeEnv, sds.IncludeEnvRegex, err = helper.SplitRegexFromMap(sds.IncludeEnv)
	if err != nil {
		// 如果出错，记录警告
		logger.Warning(sds.context.GetRuntimeContext(), "INVALID_REGEX_ALARM", "init include env regex error", err)
	}
	sds.ExcludeEnv, sds.ExcludeEnvRegex, err = helper.SplitRegexFromMap(sds.ExcludeEnv)
	if err != nil {
		// 如果出错，记录警告
		logger.Warning(sds.context.GetRuntimeContext(), "INVALID_REGEX_ALARM", "init exclude env regex error", err)
	}
	// 处理 IncludeLabel 和 ExcludeLabel
	if sds.IncludeLabel != nil {
		for k, v := range sds.IncludeContainerLabel {
			sds.IncludeLabel[k] = v
		}
	} else {
		sds.IncludeLabel = sds.IncludeContainerLabel
	}
	if sds.ExcludeLabel != nil {
		for k, v := range sds.ExcludeContainerLabel {
			sds.ExcludeLabel[k] = v
		}
	} else {
		sds.ExcludeLabel = sds.ExcludeContainerLabel
	}
	// 分割并从映射中获取正则表达式
	sds.IncludeLabel, sds.IncludeLabelRegex, err = helper.SplitRegexFromMap(sds.IncludeLabel)
	if err != nil {
		// 如果出错，记录警告
		logger.Warning(sds.context.GetRuntimeContext(), "INVALID_REGEX_ALARM", "init include label regex error", err)
	}
	sds.ExcludeLabel, sds.ExcludeLabelRegex, err = helper.SplitRegexFromMap(sds.ExcludeLabel)
	if err != nil {
		// 如果出错，记录警告
		logger.Warning(sds.context.GetRuntimeContext(), "INVALID_REGEX_ALARM", "init exclude label regex error", err)
	}
	// 创建 K8SFilter
	sds.K8sFilter, err = helper.CreateK8SFilter(sds.K8sNamespaceRegex, sds.K8sPodRegex, sds.K8sContainerRegex, sds.IncludeK8sLabel, sds.ExcludeK8sLabel)
	return 0, err // 返回错误（如果有）
}

// Description 方法返回 ServiceDockerStdout 的描述
func (sds *ServiceDockerStdout) Description() string {
	return "the container stdout input plugin for iLogtail, which supports docker and containerd."
}

// Collect 方法是一个空方法，返回 nil 错误
func (sds *ServiceDockerStdout) Collect(pipeline.Collector) error {
	return nil
}

// FlushAll 方法处理容器的更新，添加和删除
func (sds *ServiceDockerStdout) FlushAll(c pipeline.Collector, firstStart bool) error {
	newUpdateTime := helper.GetContainersLastUpdateTime() // 获取最新的更新时间
	if sds.lastUpdateTime != 0 {
		if sds.lastUpdateTime >= newUpdateTime {
			return nil // 如果没有新的更新，返回 nil
		}
	}

	var err error
	// 获取容器的更新信息
	newCount, delCount, addResultList, deleteResultList := helper.GetContainerByAcceptedInfoV2(
		sds.fullList, sds.matchList,
		sds.IncludeLabel, sds.ExcludeLabel,
		sds.IncludeLabelRegex, sds.ExcludeLabelRegex,
		sds.IncludeEnv, sds.ExcludeEnv,
		sds.IncludeEnvRegex, sds.ExcludeEnvRegex,
		sds.K8sFilter)
	sds.lastUpdateTime = newUpdateTime // 更新 lastUpdateTime

	if sds.CollectContainersFlag {
		// 如果 CollectContainersFlag 为真，记录配置结果
		{
			keys := make([]string, 0, len(sds.matchList))
			for k := range sds.matchList {
				if len(k) > 0 {
					keys = append(keys, helper.GetShortID(k))
				}
			}
			configResult := &helper.ContainerConfigResult{
				DataType:                   "container_config_result",
				Project:                    sds.context.GetProject(),
				Logstore:                   sds.context.GetLogstore(),
				ConfigName:                 sds.context.GetConfigName(),
				PathExistInputContainerIDs: helper.GetStringFromList(keys),
				SourceAddress:              "stdout",
				InputType:                  input.ServiceDockerStdoutPluginName,
				FlusherType:                "flusher_sls",
				FlusherTargetAddress:       fmt.Sprintf("%s/%s", sds.context.GetProject(), sds.context.GetLogstore()),
			}
			helper.RecordContainerConfigResultMap(configResult)
			if newCount != 0 || delCount != 0 || firstStart {
				helper.RecordContainerConfigResultIncrement(configResult)
			}
			logger.Debugf(sds.context.GetRuntimeContext(), "update match list, addResultList: %v, deleteResultList: %v", addResultList, deleteResultList)
		}
	}

	if !firstStart && newCount == 0 && delCount == 0 {
		logger.Debugf(sds.context.GetRuntimeContext(), "update match list, firstStart: %v, new: %v, delete: %v",
			firstStart, newCount, delCount)
		return nil // 如果没有新的更新，返回 nil
	}
	logger.Infof(sds.context.GetRuntimeContext(), "update match list, firstStart: %v, new: %v, delete: %v",
		firstStart, newCount, delCount)

	dockerInfos := sds.matchList // 获取 matchList
	logger.Debug(sds.context.GetRuntimeContext(), "match list length", len(dockerInfos))
	sds.avgInstanceMetric.Add(int64(len(dockerInfos))) // 添加到 avgInstanceMetric
	for id, info := range dockerInfos {
		if !logDriverSupported(info.ContainerInfo) {
			continue // 如果 logDriver 不支持，跳过
		}
		if _, ok := sds.synerMap[id]; !ok || firstStart {
			syner := NewDockerFileSyner(sds, info, sds.checkpointMap) // 创建新的 DockerFileSyner
			logger.Info(sds.context.GetRuntimeContext(), "docker stdout", "added", "source host path", info.ContainerInfo.LogPath,
				"id", info.IDPrefix(), "name", info.ContainerInfo.Name, "created", info.ContainerInfo.Created, "status", info.Status())
			sds.addMetric.Add(1)           // 增加 addMetric
			sds.synerMap[id] = syner       // 添加到 synerMap
			syner.dockerFileReader.Start() // 开始每个DockerFileSyner的读取器
		}
	}

	// 删除容器
	for id, syner := range sds.synerMap {
		// 如果dockerInfos中没有这个容器，那么删除它
		if _, ok := dockerInfos[id]; !ok {
			// 记录信息日志
			logger.Info(sds.context.GetRuntimeContext(), "docker stdout", "deleted", "id", helper.GetShortID(id), "name", syner.info.ContainerInfo.Name)
			// 停止DockerFileSyner的读取器
			syner.dockerFileReader.Stop()
			// 从synerMap中删除这个容器
			delete(sds.synerMap, id)
			// 增加删除指标
			sds.deleteMetric.Add(1)
		}
	}

	// 返回错误
	return err
}

// SaveCheckPoint函数用于保存检查点
func (sds *ServiceDockerStdout) SaveCheckPoint(force bool) error {
	checkpointChanged := false
	// 遍历synerMap
	for id, syner := range sds.synerMap {
		// 获取检查点和是否改变的标志
		checkpoint, changed := syner.dockerFileReader.GetCheckpoint()
		if changed {
			// 如果检查点改变，设置checkpointChanged为true
			checkpointChanged = true
		}
		// 更新检查点
		sds.checkpointMap[id] = checkpoint
	}
	// 如果没有强制保存，并且检查点没有改变，那么不需要保存检查点
	if !force && !checkpointChanged {
		logger.Debug(sds.context.GetRuntimeContext(), "no need to save checkpoint, checkpoint size", len(sds.checkpointMap))
		return nil
	}
	// 记录调试日志
	logger.Debug(sds.context.GetRuntimeContext(), "save checkpoint, checkpoint size", len(sds.checkpointMap))
	// 保存检查点对象
	return sds.context.SaveCheckPointObject(serviceDockerStdoutKey, sds.checkpointMap)
}

// LoadCheckPoint函数用于加载检查点
func (sds *ServiceDockerStdout) LoadCheckPoint() {
	// 如果检查点已经存在，那么直接返回
	if sds.checkpointMap != nil {
		return
	}
	// 创建新的检查点映射
	sds.checkpointMap = make(map[string]helper.LogFileReaderCheckPoint)
	// 获取检查点对象
	sds.context.GetCheckPointObject(serviceDockerStdoutKey, &sds.checkpointMap)
}

// ClearUselessCheckpoint函数用于清除无用的检查点
func (sds *ServiceDockerStdout) ClearUselessCheckpoint() {
	// 如果检查点映射不存在，那么直接返回
	if sds.checkpointMap == nil {
		return
	}
	// 遍历检查点映射
	for id := range sds.checkpointMap {
		// 如果synerMap中没有这个容器，那么删除这个检查点
		if _, ok := sds.synerMap[id]; !ok {
			// 记录信息日志
			logger.Info(sds.context.GetRuntimeContext(), "delete checkpoint, id", id)
			// 删除检查点
			delete(sds.checkpointMap, id)
		}
	}
}

// Start函数用于启动ServiceInput的服务
func (sds *ServiceDockerStdout) Start(c pipeline.Collector) error {
	// 设置收集器
	sds.collector = c
	// 创建shutdown通道
	sds.shutdown = make(chan struct{})
	// 增加等待组的计数
	sds.waitGroup.Add(1)
	// 在函数结束时减少等待组的计数
	defer sds.waitGroup.Done()

	// 加载检查点
	sds.LoadCheckPoint()

	// 设置最后保存检查点的时间
	lastSaveCheckPointTime := time.Now()

	// 刷新所有的DockerFileSyner
	_ = sds.FlushAll(c, true)
	// 循环处理
	for {
		// 创建定时器
		timer := time.NewTimer(time.Duration(sds.FlushIntervalMs) * time.Millisecond)
		select {
		// 如果收到shutdown通道的信号，那么停止所有的DockerFileSyner并返回
		case <-sds.shutdown:
			logger.Info(sds.context.GetRuntimeContext(), "docker stdout main runtime stop", "begin")
			for _, syner := range sds.synerMap {
				syner.dockerFileReader.Stop()
			}
			logger.Info(sds.context.GetRuntimeContext(), "docker stdout main runtime stop", "success")
			return nil
		// 如果定时器到时，那么保存检查点并清除无用的检查点，然后刷新所有的DockerFileSyner
		case <-timer.C:
			if nowTime := time.Now(); nowTime.Sub(lastSaveCheckPointTime) > time.Second*time.Duration(sds.SaveCheckPointSec) {
				_ = sds.SaveCheckPoint(false)
				lastSaveCheckPointTime = nowTime
				sds.ClearUselessCheckpoint()
			}
			_ = sds.FlushAll(c, false)
		}
	}
}

// Stop函数用于停止服务并关闭所有必要的通道和连接
func (sds *ServiceDockerStdout) Stop() error {
	// 关闭shutdown通道
	close(sds.shutdown)
	// 等待所有的goroutine结束
	sds.waitGroup.Wait()
	// 强制保存检查点
	_ = sds.SaveCheckPoint(true)
	// 返回nil表示没有错误
	return nil
}

func init() {
	pipeline.ServiceInputs[input.ServiceDockerStdoutPluginName] = func() pipeline.ServiceInput {
		return &ServiceDockerStdout{
			FlushIntervalMs:      3000,
			SaveCheckPointSec:    60,
			ReadIntervalMs:       1000,
			Stdout:               true,
			Stderr:               true,
			BeginLineTimeoutMs:   3000,
			LogtailInDocker:      true,
			CloseUnChangedSec:    60,
			BeginLineCheckLength: 10 * 1024,
			MaxLogSize:           512 * 1024,
			StartLogMaxOffset:    128 * 1024,
		}
	}
}
