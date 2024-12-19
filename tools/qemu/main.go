package main

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/exec"
	"strings"
	"time"

	"golang.org/x/crypto/ssh"
)

var logger *log.Logger

// CPUTest 存储CPU测试结果
type CPUTest struct {
	Model   string
	Working bool
	Error   error
	Log     string
}

// 获取QEMU支持的CPU列表
func getQEMUCPUs() ([]string, error) {
	cmd := exec.Command("qemu-system-x86_64", "-cpu", "help")
	var out bytes.Buffer
	cmd.Stdout = &out

	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("执行QEMU命令失败: %v", err)
	}

	var cpus []string
	scanner := bufio.NewScanner(&out)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.Contains(line, "x86") {
			fields := strings.Fields(line)
			if len(fields) > 0 {
				cpus = append(cpus, fields[1])
			}
		}
	}

	return cpus, nil
}

// 测试单个CPU型号
func testCPU(ctx context.Context, model string) CPUTest {
	result := CPUTest{Model: model}

	// 为每个测试创建COW镜像
	imgName := fmt.Sprintf("test-%s.img", model)
	createImg := exec.Command("qemu-img", "create", "-f", "qcow2",
		"-b", "disk.img", "-F", "qcow2", imgName)
	if err := createImg.Run(); err != nil {
		result.Error = fmt.Errorf("创建测试镜像失败: %v", err)
		return result
	}
	defer os.Remove(imgName) // 测试完成后清理

	// 创建带有超时的上下文
	timeout := 10 * time.Minute
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	startTime := time.Now()
	defer func() {
		logger.Printf("测试CPU型号: %s, 耗时: %v\n", model, time.Since(startTime).Seconds())
	}()
	// 创建QEMU命令
	cmd := exec.CommandContext(ctx, "qemu-system-x86_64",
		"-cpu", model,
		"-m", "1024",
		"-smp", "2",
		"-hda", imgName,
		"-netdev", "user,id=net0,hostfwd=tcp::2222-:22",
		"-device", "e1000,netdev=net0",
		"-nographic")

	var cmdBuffer bytes.Buffer
	// 重定向命令的输出到日志文件
	cmd.Stdout = &cmdBuffer
	cmd.Stderr = &cmdBuffer

	// 启动QEMU
	if err := cmd.Start(); err != nil {
		result.Error = fmt.Errorf("启动失败: %v", err)
		return result
	}
	defer func() {
		if err := cmd.Process.Kill(); err != nil {
			logger.Printf("杀死QEMU进程失败: %v\n", err)
		}
	}() // 确保进程被终止
	// 创建一个channel用于监控输出
	outputDone := make(chan bool)
	go func() {
		timeout := time.After(60 * time.Second)
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()

		for {
			select {
			case <-ticker.C:
				if strings.Contains(cmdBuffer.String(), "Linux version") {
					outputDone <- true
					return
				}
			case <-timeout:
				outputDone <- false
				return
			}
		}
	}()
	// 等待输出检查结果
	if ok := <-outputDone; !ok {
		result.Error = fmt.Errorf("启动超时：60秒内未检测到Linux启动信息, cmdBuffer: %s", cmdBuffer.String())
		return result
	}

	checkSSHSuccess := false
	// 等待系统启动并尝试SSH连接
	for time.Since(startTime) < timeout {
		if checkSSH("0.0.0.0", "2222") {
			checkSSHSuccess = true
			break
		}
		time.Sleep(2 * time.Second)
	}
	if !checkSSHSuccess {
		result.Error = fmt.Errorf("SSH连接失败, cmdBuffer: %s", cmdBuffer.String())
		return result
	}

	// 执行测试命令
	outBuf, err := runSSHCommand(`wget https://aliyun-observability-release-cn-heyuan.oss-cn-heyuan.aliyuncs.com/loongcollector/linux64/0.2.0-test/loongcollector_0.2.0 -O loongcollector_test && sudo chmod a+x loongcollector_test`)
	if err != nil {
		result.Error = fmt.Errorf("下载loongcollector失败, err: %v\n输出: %s", err, outBuf)
		return result
	}
	outBuf, err = runSSHCommand(`lscpu`)
	if !strings.Contains(outBuf, "Model name:") {
		result.Error = fmt.Errorf("获取lscpu失败, err: %v\n输出: %s", err, outBuf)
		return result
	}
	result.Log = outBuf
	outBuf, err = runSSHCommand(`./loongcollector_test`)
	if err != nil || !strings.Contains(outBuf, "not existing, create done") {
		result.Error = fmt.Errorf("测试loongcollector失败, err: %v\n输出: %s", err, outBuf)
		return result
	}

	result.Working = true
	return result
}

// 执行SSH命令的辅助函数
func runSSHCommand(command string) (string, error) {
	var outBuf bytes.Buffer
	cmd := exec.Command("ssh", "-p", "2222", "-o", "StrictHostKeyChecking=no", "root@0.0.0.0", command)
	cmd.Stdout = &outBuf
	cmd.Stderr = &outBuf
	err := cmd.Run()
	return outBuf.String(), err
}

// 检查SSH连接
func checkSSH(host string, port string) bool {
	// 尝试TCP连接
	conn, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%s", host, port), 5*time.Second)
	if err != nil {
		return false
	}
	conn.Close()

	// 尝试SSH连接
	sshConfig := &ssh.ClientConfig{
		User: "root",
		Auth: []ssh.AuthMethod{
			ssh.Password(""), // 空密码
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
		Timeout:         5 * time.Second,
	}

	sshClient, err := ssh.Dial("tcp", fmt.Sprintf("%s:%s", host, port), sshConfig)
	if err != nil {
		return false
	}
	defer sshClient.Close()

	return true
}

func main() {
	// 创建日志文件
	f, err := os.OpenFile("cpu_test.log", os.O_RDWR|os.O_CREATE|os.O_APPEND, 0666)
	if err != nil {
		log.Fatalf("创建日志文件失败: %v", err)
	}
	defer f.Close()

	// 创建多输出writer
	multiWriter := io.MultiWriter(os.Stdout, f)
	logger = log.New(multiWriter, "", log.LstdFlags)

	// 获取CPU列表
	cpus, err := getQEMUCPUs()
	if err != nil {
		logger.Fatalf("获取CPU列表失败: %v", err)
	}
	logger.Printf("发现 %d 个CPU型号\n", len(cpus))

	var working, failed int
	for i, cpu := range cpus {
		logger.Printf("开始测试CPU型号: %s, index: %d\n", cpu, i)
		result := testCPU(context.Background(), cpu)
		if result.Working {
			logger.Printf("✓ %s index: %d: 正常工作\n%s\n", result.Model, i, result.Log)
			working++
		} else {
			logger.Printf("✗ %s index: %d, err: %v\nlscpu:%s\n", result.Model, i, result.Error, result.Log)
			failed++
		}
	}

	// 打印统计信息
	logger.Printf("\n测试完成:\n")
	logger.Printf("总计: %d\n", len(cpus))
	logger.Printf("成功: %d\n", working)
	logger.Printf("失败: %d\n", failed)
}
