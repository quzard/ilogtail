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

// 这是一个Go语言项目的片段，它涉及到日志文件读取和处理。以下是代码的主要组成部分和函数的功能，以及它们之间的链路关系。

// 结构体和函数的功能：
// ReaderMetricTracker 结构体: 用于跟踪日志文件读取器（LogFileReader）的各种度量指标，如打开文件、关闭文件的次数，文件大小，文件旋转（如替换或删除）、读取操作的次数和大小，以及处理延迟。

// NewReaderMetricTracker 函数: 创建并初始化一个新的ReaderMetricTracker实例。

// LogFileReaderConfig 结构体: 定义用于配置LogFileReader如读取间隔、最大读取块大小、关闭文件的秒数和跟踪器对象等参数。

// DefaultLogFileReaderConfig 变量: 存储默认的LogFileReaderConfig实例。

// LogFileReaderCheckPoint 结构体: 用于记录日志文件读取的检查点，包括文件路径、偏移量和文件状态。

// IsSame 函数: 用于比较两个检查点是否相同。

// LogFileReader 结构体: 用于从日志文件中读取日志信息，并包含配置、文件对象、检查点、处理器、当前块、缓冲区大小、读取标志、关闭通道、日志上下文、等待组、文件状态等信息。

// NewLogFileReader 函数: 根据提供的配置、检查点和处理器创建一个新的LogFileReader实例。

// GetLastEndOfLine 方法: 返回最后一个以'\n'结尾的位置。

// CheckFileChange 方法: 检查日志文件是否发生变化。

// GetProcessor 方法: 返回与LogFileReader关联的日志文件处理器。

// GetCheckpoint 方法: 获取当前检查点和一个布尔值，指示检查点是否已更新。

// UpdateProcessResult 方法: 根据读取和处理的结果更新内部状态和检查点偏移量。

// ProcessAfterRead 方法: 处理读取到的日志块。

// ReadOpen 方法: 打开日志文件准备读取。

// ReadAndProcess 方法: 读取日志文件，并调用处理器处理日志。

// CloseFile 方法: 关闭当前打开的日志文件。

// Run 方法: 启动日志文件读取器的主循环。

// SetForceRead 方法: 设置日志文件读取器在下一次循环时强制开始读取。

// Start 方法: 启动日志文件读取器，创建关闭通道和等待组并在新goroutine中运行。

// Stop 方法: 停止日志文件读取器，关闭通道并等待goroutine结束。

// 链路关系：
// NewLogFileReader 创建并初始化 LogFileReader 实例，它使用 LogFileReaderConfig 和 LogFileReaderCheckPoint 作为配置和状态信息。
// LogFileReader 实例使用 ReaderMetricTracker 来跟踪日志文件读取器的度量。
// LogFileReader 的 Run 方法负责执行读取和处理的主循环，使用如 ReadOpen, ReadAndProcess, CloseFile 等方法控制日志文件的读取和处理。
// Start 方法用于启动LogFileReader的主循环，而 Stop 方法用于停止它。
// LogFileReader 在读取日志文件过程中会使用 LogFileProcessor 接口的实现来处理读取的数据，但具体的处理器实现没有在这段代码中体现。
// 这段代码的主体是处理日志文件，它的执行流程大致是：首先创建LogFileReader实例，然后开始执行日志文件的读取和处理，在这个过程中会不断检查文件是否发生变化，并且会根据配置决定何时读取、处理、关闭等。LogFileReader通过Start方法开始其工作，会在一个新的goroutine中运行。在其运行过程中，LogFileReader会使用ReadAndProcess来不断读取日志文件的数据，并通过配置的LogFileProcessor进行处理。

// 具体来说，ReadAndProcess方法会调用ReadOpen来尝试打开文件，如果文件已经被打开，会直接读取数据；接着，会使用ReadAt函数来从文件中读取数据块，将其传递给LogFileProcessor处理。LogFileReader还会跟踪文件变化，如果检测到文件发生变化（如被截断或者inode变化），则会相应地调整读取位置或重新打开文件。

// 在整个读取过程中，LogFileReader会使用ReaderMetricTracker中定义的度量指标来跟踪读取文件的次数、读取的大小、文件旋转次数等信息，以便监控性能和行为。

// Stop方法用于停止读取器，它会通过关闭shutdown通道来发送停止信号，这会导致Run方法中的主循环退出，并且在退出前确保所有数据都已正确处理。

// LogFileReader中的GetLastEndOfLine、ProcessAfterRead、UpdateProcessResult等方法都是为了处理读取的数据并确保数据的正确性和完整性。例如，它会在数据块的末尾寻找最后一个换行符，以保证完整的日志行被正确处理。如果读取的数据块不是以换行符结尾，它会保存剩余的部分，并在下一次迭代中处理这些剩余数据。

// 整体上，这段代码定义了一个能够持续读取日志文件并且跟踪读取性能指标的日志文件读取器。它能够处理文件变化，包括文件截断或者旋转，并且确保日志数据被正确读取和处理。

package helper

import (
	"github.com/alibaba/ilogtail/pkg/logger"
	"github.com/alibaba/ilogtail/pkg/pipeline"
	"github.com/alibaba/ilogtail/pkg/util"

	"context"
	"io"
	"os"
	"sync"
	"time"
)

// ReaderMetricTracker 结构体，用于跟踪读取器的各种度量指标
type ReaderMetricTracker struct {
	OpenCounter        pipeline.CounterMetric // 打开文件的计数器
	CloseCounter       pipeline.CounterMetric // 关闭文件的计数器
	FileSizeCounter    pipeline.CounterMetric // 文件大小的计数器
	FileRotatorCounter pipeline.CounterMetric // 文件旋转的计数器
	ReadCounter        pipeline.CounterMetric // 读取操作的计数器
	ReadSizeCounter    pipeline.CounterMetric // 读取大小的计数器
	ProcessLatency     pipeline.LatencyMetric // 处理延迟的度量
}

// NewReaderMetricTracker 函数，用于创建一个新的 ReaderMetricTracker 实例
func NewReaderMetricTracker() *ReaderMetricTracker {
	return &ReaderMetricTracker{
		OpenCounter:        NewCounterMetric("open_count"),          // 打开文件的计数器
		CloseCounter:       NewCounterMetric("close_count"),         // 关闭文件的计数器
		FileSizeCounter:    NewCounterMetric("file_size"),           // 文件大小的计数器
		FileRotatorCounter: NewCounterMetric("file_rotate"),         // 文件旋转的计数器
		ReadCounter:        NewCounterMetric("read_count"),          // 读取操作的计数器
		ReadSizeCounter:    NewCounterMetric("read_size"),           // 读取大小的计数器
		ProcessLatency:     NewLatencyMetric("log_process_latency"), // 处理延迟的度量
	}
}

// LogFileReaderConfig 结构体，用于配置日志文件读取器
type LogFileReaderConfig struct {
	ReadIntervalMs   int                  // 读取间隔（毫秒）
	MaxReadBlockSize int                  // 最大读取块大小
	CloseFileSec     int                  // 关闭文件的秒数
	Tracker          *ReaderMetricTracker // 读取器度量跟踪器
}

// DefaultLogFileReaderConfig 是默认的日志文件读取器配置
var DefaultLogFileReaderConfig = LogFileReaderConfig{
	ReadIntervalMs:   1000,       // 读取间隔（毫秒）
	MaxReadBlockSize: 512 * 1024, // 最大读取块大小
	CloseFileSec:     60,         // 关闭文件的秒数
	Tracker:          nil,        // 读取器度量跟踪器
}

// LogFileReaderCheckPoint 结构体，用于记录日志文件读取器的检查点
type LogFileReaderCheckPoint struct {
	Path   string  // 文件路径
	Offset int64   // 偏移量
	State  StateOS // 状态
}

// IsSame 函数，用于检查两个检查点是否相同
func (checkpoint *LogFileReaderCheckPoint) IsSame(other *LogFileReaderCheckPoint) bool {
	if checkpoint.Path != other.Path || checkpoint.Offset != other.Offset {
		return false
	}
	return !checkpoint.State.IsChange(other.State)
}

// LogFileReader 结构体，用于读取日志文件
type LogFileReader struct {
	Config LogFileReaderConfig // 读取器配置

	file           *os.File                // 文件对象
	lastCheckpoint LogFileReaderCheckPoint // 上一个检查点
	checkpoint     LogFileReaderCheckPoint // 当前检查点
	processor      LogFileProcessor        // 日志文件处理器
	nowBlock       []byte                  // 当前块
	lastBufferSize int                     // 上一个缓冲区大小
	lastBufferTime time.Time               // 上一个缓冲区时间
	readWhenStart  bool                    // 开始时是否读取
	checkpointLock sync.Mutex              // 检查点锁
	shutdown       chan struct{}           // 关闭通道
	logContext     context.Context         // 日志上下文
	waitgroup      sync.WaitGroup          // 等待组
	foundFile      bool                    // 是否找到文件
}

// NewLogFileReader 函数，用于创建一个新的日志文件读取器
func NewLogFileReader(context context.Context, checkpoint LogFileReaderCheckPoint, config LogFileReaderConfig, processor LogFileProcessor) (*LogFileReader, error) {
	readWhenStart := false // 开始时是否读取
	foundFile := true      // 是否找到文件
	// 如果检查点的状态为空
	if checkpoint.State.IsEmpty() {
		// 获取文件的状态
		if newStat, err := os.Stat(checkpoint.Path); err == nil {
			checkpoint.State = GetOSState(newStat) // 更新检查点的状态
		} else {
			// 如果文件不存在
			if os.IsNotExist(err) {
				foundFile = false // 更新找到文件的标志
			}
			// 记录警告日志
			logger.Warning(context, "STAT_FILE_ALARM", "stat file error when create reader, file", checkpoint.Path, "error", err.Error())
		}
	}
	// 如果检查点的状态不为空
	if !checkpoint.State.IsEmpty() {
		// 计算当前时间与文件修改时间的差值
		if deltaNano := time.Now().UnixNano() - int64(checkpoint.State.ModifyTime); deltaNano >= 0 && deltaNano < 180*1e9 {
			readWhenStart = true // 更新开始时是否读取的标志
			// 记录信息日志
			logger.Info(context, "read file", checkpoint.Path, "first read", readWhenStart)
		} else {
			// 记录信息日志
			logger.Info(context, "read file", checkpoint.Path, "since offset", checkpoint.Offset)
		}
	}
	// 返回新创建的日志文件读取器
	return &LogFileReader{
		checkpoint:     checkpoint,
		Config:         config,
		processor:      processor,
		logContext:     context,
		lastBufferSize: 0,
		nowBlock:       nil,
		readWhenStart:  readWhenStart,
		foundFile:      foundFile,
	}, nil
}

// GetLastEndOfLine 方法，返回新读取的字节以 '\n' 结尾的位置
// @note 当 n + r.lastBufferSize == len(r.nowBlock) 时，将返回 n + r.lastBufferSize
func (r *LogFileReader) GetLastEndOfLine(n int) int {
	blockSize := n + r.lastBufferSize // 计算块大小
	// 如果块大小等于当前块的长度，返回 n
	if blockSize == len(r.nowBlock) {
		return n
	}
	// 从后向前遍历当前块
	for i := blockSize - 1; i >= r.lastBufferSize; i-- {
		// 如果找到 '\n'，返回其位置
		if r.nowBlock[i] == '\n' {
			return i - r.lastBufferSize + 1
		}
	}
	return 0
}

// CheckFileChange 方法，检查文件是否发生变化
func (r *LogFileReader) CheckFileChange() bool {
	// 获取文件的新状态
	if newStat, err := os.Stat(r.checkpoint.Path); err == nil {
		newOsStat := GetOSState(newStat) // 获取新的操作系统状态
		// 如果检查点的状态发生变化
		if r.checkpoint.State.IsChange(newOsStat) {
			needResetOffset := false // 是否需要重置偏移量
			// 如果文件发生变化
			if r.checkpoint.State.IsFileChange(newOsStat) {
				needResetOffset = true // 更新重置偏移量的标志
				// 记录信息日志
				logger.Info(r.logContext, "file dev inode changed, read to end and force read from beginning, file", r.checkpoint.Path, "old", r.checkpoint.State.String(), "new", newOsStat.String(), "offset", r.checkpoint.Offset)
				// 读取到文件末尾或关闭文件
				if r.file != nil {
					r.ReadAndProcess(false)
					r.CloseFile("open file and dev inode changed")
				}
				// 如果有跟踪器，增加文件旋转的计数
				if r.Config.Tracker != nil {
					r.Config.Tracker.FileRotatorCounter.Add(1)
				}
				// 如果文件发生变化，强制刷新最后的缓冲区
				if r.lastBufferSize > 0 {
					processSize := r.processor.Process(r.nowBlock[0:r.lastBufferSize], time.Hour)
					r.UpdateProcessResult(0, processSize)
				}
			}
			// 锁定检查点
			r.checkpointLock.Lock()
			r.checkpoint.State = newOsStat // 更新检查点的状态
			// 如果需要重置偏移量
			if needResetOffset {
				r.checkpoint.Offset = 0 // 重置偏移量
			}
			// 解锁检查点
			r.checkpointLock.Unlock()
			return true
		}
		r.foundFile = true // 更新找到文件的标志
	} else {
		// 如果文件不存在
		if os.IsNotExist(err) {
			if r.foundFile {
				// 记录警告日志
				logger.Warning(r.logContext, "STAT_FILE_ALARM", "stat file error, file", r.checkpoint.Path, "error", err.Error())
				r.foundFile = false // 更新找到文件的标志
			}
		} else {
			// 记录警告日志
			logger.Warning(r.logContext, "STAT_FILE_ALARM", "stat file error, file", r.checkpoint.Path, "error", err.Error())
		}

	}
	return false
}

// GetProcessor 方法，返回日志文件处理器
func (r *LogFileReader) GetProcessor() LogFileProcessor {
	return r.processor
}

// GetCheckpoint 方法，获取当前的检查点和更新标志
func (r *LogFileReader) GetCheckpoint() (checkpoint LogFileReaderCheckPoint, updateFlag bool) {
	r.checkpointLock.Lock() // 加锁
	defer func() {          // 在函数结束时执行
		r.lastCheckpoint = r.checkpoint // 更新最后的检查点
		r.checkpointLock.Unlock()       // 解锁
	}()
	r.lastCheckpoint = r.checkpoint                             // 更新最后的检查点
	return r.checkpoint, r.lastCheckpoint.IsSame(&r.checkpoint) // 返回当前的检查点和更新标志
}

// UpdateProcessResult 方法，更新处理结果
func (r *LogFileReader) UpdateProcessResult(readN, processedN int) {
	if readN+r.lastBufferSize == processedN { // 如果读取的大小加上最后的缓冲区大小等于处理的大小
		r.lastBufferSize = 0          // 清空最后的缓冲区大小
		r.lastBufferTime = time.Now() // 更新最后的缓冲区时间
	} else {
		if processedN != 0 { // 如果处理的大小不为0
			// 需要移动缓冲区
			copy(r.nowBlock, r.nowBlock[processedN:readN+r.lastBufferSize]) // 复制缓冲区
			r.lastBufferTime = time.Now()                                   // 更新最后的缓冲区时间
		}
		r.lastBufferSize = readN + r.lastBufferSize - processedN // 更新最后的缓冲区大小
	}
	r.checkpointLock.Lock()                  // 加锁
	defer r.checkpointLock.Unlock()          // 在函数结束时解锁
	r.checkpoint.Offset += int64(processedN) // 更新检查点的偏移量
}

// ProcessAfterRead 方法，读取后的处理
func (r *LogFileReader) ProcessAfterRead(n int) {
	// 如果没有更多的文件，检查并处理最后的缓冲区
	if n == 0 {
		if r.lastBufferSize > 0 { // 如果最后的缓冲区大小大于0
			// 处理最后的缓冲区
			processSize := r.processor.Process(r.nowBlock[0:r.lastBufferSize], time.Since(r.lastBufferTime))
			if processSize > n+r.lastBufferSize { // 如果处理的大小大于n加上最后的缓冲区大小
				processSize = n + r.lastBufferSize // 更新处理的大小
			}
			r.UpdateProcessResult(n, processSize) // 更新处理结果
		} else {
			// 如果没有数据，只调用处理并给一个空的字节切片
			r.processor.Process(r.nowBlock[0:0], time.Since(r.lastBufferTime))
		}
	} else {
		// 处理当前的缓冲区
		processSize := r.processor.Process(r.nowBlock[0:r.lastBufferSize+n], time.Duration(0))
		if processSize > n+r.lastBufferSize { // 如果处理的大小大于n加上最后的缓冲区大小
			processSize = n + r.lastBufferSize // 更新处理的大小
		}
		r.UpdateProcessResult(n, processSize) // 更新处理结果
	}
}

// ReadOpen 方法，打开文件进行读取
func (r *LogFileReader) ReadOpen() error {
	if r.file == nil { // 如果文件为空
		var err error
		r.file, err = ReadOpen(r.checkpoint.Path) // 打开文件进行读取
		if r.Config.Tracker != nil {              // 如果有跟踪器
			r.Config.Tracker.OpenCounter.Add(1) // 增加打开计数
		}
		logger.Debug(r.logContext, "open file for read, file", r.checkpoint.Path, "offset", r.checkpoint.Offset, "status", r.checkpoint.State) // 记录调试日志
		return err                                                                                                                             // 返回错误
	}
	return nil // 返回nil
}

// ReadAndProcess 方法，读取并处理日志文件
func (r *LogFileReader) ReadAndProcess(once bool) {
	if r.nowBlock == nil { // 如果当前的块为空
		// once 只在关闭时为真，我们不需要在关闭时初始化 r.nowBlock，因为这个文件从未被读取过
		if once {
			return
		}
		// 延迟初始化
		r.nowBlock = make([]byte, r.Config.MaxReadBlockSize) // 创建一个新的块
	}
	if err := r.ReadOpen(); err == nil { // 如果打开文件没有错误
		file := r.file // 获取文件
		// 双重检查
		if newStat, statErr := file.Stat(); statErr == nil { // 如果获取文件状态没有错误
			newOsStat := GetOSState(newStat) // 获取新的操作系统状态

			// 检查文件设备+索引节点是否改变
			if r.checkpoint.State.IsFileChange(newOsStat) {
				// 如果文件设备+索引节点改变，强制从头开始读取
				// 如果最后的缓冲区大小大于0，强制刷新最后的缓冲区
				if r.lastBufferSize > 0 {
					processSize := r.processor.Process(r.nowBlock[0:r.lastBufferSize], time.Hour)
					r.UpdateProcessResult(0, processSize) // 更新处理结果
				}
				if r.Config.Tracker != nil {
					r.Config.Tracker.FileRotatorCounter.Add(1) // 增加文件旋转计数
				}
				r.checkpointLock.Lock()             // 加锁
				r.checkpoint.Offset = 0             // 设置检查点的偏移量为0
				r.checkpoint.State = newOsStat      // 更新检查点的状态
				r.checkpointLock.Unlock()           // 解锁
				r.CloseFile("file changed(rotate)") // 关闭文件
				return
			}

			// 检查文件是否被截断
			if newOsStat.Size < r.checkpoint.Offset {
				// 如果文件被截断，强制从头开始读取
				// 如果最后的缓冲区大小大于0，强制刷新最后的缓冲区
				if r.lastBufferSize > 0 {
					processSize := r.processor.Process(r.nowBlock[0:r.lastBufferSize], time.Hour)
					r.UpdateProcessResult(0, processSize) // 更新处理结果
				}
				if r.Config.Tracker != nil {
					r.Config.Tracker.FileRotatorCounter.Add(1) // 增加文件旋转计数
				}
				r.checkpointLock.Lock() // 加锁
				if newOsStat.Size < 10*1024*1024 {
					r.checkpoint.Offset = 0 // 设置检查点的偏移量为0
				} else {
					r.checkpoint.Offset = newOsStat.Size - 1024*1024 // 设置检查点的偏移量为新的操作系统状态的大小减去1MB
				}
				r.checkpoint.State = newOsStat        // 更新检查点的状态
				r.checkpointLock.Unlock()             // 解锁
				r.CloseFile("file changed(truncate)") // 关闭文件
				return
			}
		} else {
			// 如果获取文件状态出错，记录警告日志
			logger.Warning(r.logContext, "STAT_FILE_ALARM", "stat file error, file", r.checkpoint.Path, "error", statErr.Error())
		}
		for {
			// 读取文件
			n, readErr := file.ReadAt(r.nowBlock[r.lastBufferSize:], int64(r.lastBufferSize)+r.checkpoint.Offset)
			needBreak := false // 是否需要中断的标志
			if r.Config.Tracker != nil {
				r.Config.Tracker.ReadCounter.Add(1)            // 增加读取计数
				r.Config.Tracker.ReadSizeCounter.Add(int64(n)) // 增加读取大小计数
			}
			if once || n < r.Config.MaxReadBlockSize-r.lastBufferSize {
				needBreak = true // 如果只读取一次或者读取的大小小于最大读取块大小减去最后的缓冲区大小，设置需要中断的标志为真
			}
			if readErr != nil {
				if readErr != io.EOF {
					// 如果读取错误不是文件结束，记录警告日志并中断
					logger.Warning(r.logContext, "READ_FILE_ALARM", "read file error, file", r.checkpoint.Path, "error", readErr.Error())
					break
				}
				// 如果读取到文件结束，记录调试日志并设置需要中断的标志为真
				logger.Debug(r.logContext, "read end of file", r.checkpoint.Path, "offset", r.checkpoint.Offset, "last buffer size", r.lastBufferSize, "read n", n, "stat", r.checkpoint.State.String())
				needBreak = true
			}
			// 只接受以'\n'结束的缓冲区
			n = r.GetLastEndOfLine(n)
			// 处理读取后的数据
			r.ProcessAfterRead(n)
			if !needBreak {
				// 检查是否需要关闭
				select {
				case <-r.shutdown:
					// 如果接收到停止信号，记录信息日志并设置需要中断的标志为真
					logger.Info(r.logContext, "receive stop signal when read data, path", r.checkpoint.Path)
					needBreak = true
				default:
				}
			}
			if needBreak {
				break // 如果需要中断，跳出循环
			}
		}
	} else {
		// 如果打开文件出错，记录警告日志
		logger.Warning(r.logContext, "READ_FILE_ALARM", "open file for read error, file", r.checkpoint.Path, "error", err.Error())
	}
}

// CloseFile 方法，关闭日志文件
func (r *LogFileReader) CloseFile(reason string) {
	if r.file != nil { // 如果文件不为空
		_ = r.file.Close()           // 关闭文件
		r.file = nil                 // 将文件设置为nil
		if r.Config.Tracker != nil { // 如果跟踪器不为空
			r.Config.Tracker.CloseCounter.Add(1) // 增加关闭计数
		}
		// 记录调试日志，包括关闭原因、文件路径、偏移量和状态
		logger.Debug(r.logContext, "close file, reason", reason, "file", r.checkpoint.Path, "offset", r.checkpoint.Offset, "status", r.checkpoint.State)
	}
}

// Run 方法，运行日志文件读取器
func (r *LogFileReader) Run() {
	defer func() { // 延迟执行的函数
		r.CloseFile("run done")                       // 关闭文件，原因是"run done"
		r.waitgroup.Done()                            // 减少等待组的计数
		panicRecover(r.logContext, r.checkpoint.Path) // 恢复panic
	}()
	lastReadTime := time.Now()  // 最后读取时间
	tracker := r.Config.Tracker // 跟踪器
	for {                       // 循环
		startProcessTime := time.Now() // 开始处理时间
		if tracker != nil {            // 如果跟踪器不为空
			tracker.ProcessLatency.Begin() // 开始处理延迟
		}
		if r.readWhenStart || r.CheckFileChange() { // 如果开始时读取或者检查到文件变化
			r.readWhenStart = false         // 设置开始时读取为假
			r.ReadAndProcess(false)         // 读取并处理文件
			lastReadTime = startProcessTime // 更新最后读取时间
		} else {
			r.ProcessAfterRead(0)                                                                      // 处理读取后的数据
			if startProcessTime.Sub(lastReadTime) > time.Second*time.Duration(r.Config.CloseFileSec) { // 如果开始处理时间减去最后读取时间大于关闭文件的秒数
				r.CloseFile("no read timeout")  // 关闭文件，原因是"no read timeout"
				lastReadTime = startProcessTime // 更新最后读取时间
			}
		}
		if tracker != nil { // 如果跟踪器不为空
			tracker.ProcessLatency.End() // 结束处理延迟
		}
		endProcessTime := time.Now()                                                                                    // 结束处理时间
		sleepDuration := time.Millisecond*time.Duration(r.Config.ReadIntervalMs) - endProcessTime.Sub(startProcessTime) // 计算睡眠时间
		if util.RandomSleep(sleepDuration, 0.1, r.shutdown) {                                                           // 随机睡眠
			r.ReadAndProcess(true)    // 读取并处理文件
			if r.lastBufferSize > 0 { // 如果最后的缓冲区大小大于0
				processSize := r.processor.Process(r.nowBlock[0:r.lastBufferSize], time.Hour) // 处理数据
				r.UpdateProcessResult(0, processSize)                                         // 更新处理结果
			}
			r.processor.Process(r.nowBlock[0:0], time.Hour) // 处理数据
			break                                           // 跳出循环
		}
	}
}

// SetForceRead 方法，设置强制开始时读取
func (r *LogFileReader) SetForceRead() {
	r.readWhenStart = true // 设置开始时读取为真
}

// Start 方法，开始日志文件读取器
func (r *LogFileReader) Start() {
	r.shutdown = make(chan struct{}) // 创建停止信号通道
	r.waitgroup.Add(1)               // 增加等待组的计数
	go r.Run()                       // 在新的goroutine中运行日志文件读取器
}

// Stop 方法，停止日志文件读取器
func (r *LogFileReader) Stop() {
	close(r.shutdown)  // 关闭停止信号通道
	r.waitgroup.Wait() // 等待所有的goroutine结束
}
