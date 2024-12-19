# qemu测试

## 安装qemu

推荐用ubuntu系统

```shell
sudo apt update
sudo apt install -y qemu-system
```

## 创建硬盘镜像

硬盘镜像是一个文件，用来存储虚拟机硬盘上的内容。qemu 支持的镜像有两种格式，分别是 `raw` 和 `qcow2`。raw 格式 I/O 开销小，但是占用全量空间 (分配大小即为实际大小)；qcow2 格式仅当系统实际写入内容时，才会分配空间，另外还支持快照。这里采用 qcow2 格式

```shell
qemu-img create -f qcow2 disk.img 60G
```

## 准备安装介质

下载 ubuntu 镜像。

```shell
wget https://releases.ubuntu.com/22.04/ubuntu-22.04.5-live-server-amd64.iso
```

## 挂载ISO

```shell
sudo mkdir /mnt/tmp_ubuntu_2204_live_server
sudo mount ./ubuntu-22.04.5-live-server-amd64.iso /mnt/tmp_ubuntu_2204_live_server -o loop
```

## 安装操作系统

使用以下命令启动QEMU并安装Ubuntu系统。这里使用了无图形界面模式,通过串口控制台进行安装:

```shell
qemu-system-x86_64 -hda disk.img\
    -cdrom ./ubuntu-22.04.5-live-server-amd64.iso\
    -boot d\
    -m 4096\
    -smp 3\
    -net nic\
    -net user\
    -nographic\
    -append console=ttyS0\
    -kernel /mnt/tmp_ubuntu_2204_live_server/casper/vmlinuz\
    -initrd /mnt/tmp_ubuntu_2204_live_server/casper/initrd
```

以 `qemu-system-*` 格式命名的程序有很多，其中 `*` 处代表的是客户机 (Guest) 对应的架构。本文中是在起 x86 的虚拟机，所以选择 `qemu-system-x86_64`

- 参数说明
    - 基础配置参数:
        - `-m <size>`: 指定客户机内存大小,单位为MB
        - `-smp <n>`: 指定CPU核心数量
    - 存储相关参数:
        - `-hda <file>`: 指定第一个IDE硬盘镜像
        - `-cdrom <file>`: 指定光驱所使用的镜像文件
        - `-drive`: 定义一个新的驱动器,可指定更多详细参数
    - 启动相关参数:
        - `-boot [order=]<drives>`: 指定启动顺序,drives可以是:
            - a/b: 软盘1/2
            - c: 第一个硬盘
            - d: 第一个光驱
        - `-kernel <file>`: 指定直接启动的内核文件
        - `-initrd <file>`: 指定initramfs文件
        - `-append <cmdline>`: 指定内核启动参数
    - 网络相关参数:
        - `-net nic`: 创建一个新的网卡
        - `-net user`: 使用QEMU内置的用户模式网络栈
    - 显示相关参数:
        - `-nographic`: 完全禁用图形输出,使用串口控制台

更多参数相关请参考 [qemu 文档](https://www.qemu.org/docs/master/system/invocation.html) 

### 运行系统

安装完成后,我们需要调整启动参数来运行系统:

1. 移除 cdrom 相关参数,因为不再需要安装镜像
2. 移除 `-boot` 参数,让系统默认从硬盘启动
3. 配置网络以支持 SSH 远程访问

下面的命令通过端口映射的方式,将虚拟机(guest)的 22 端口(SSH)映射到主机(host)的 2222 端口:

```shell
qemu-system-x86_64\
    -hda disk.img\
    -netdev user,id=net0,hostfwd=tcp::2222-:22\
    -device e1000,netdev=net0\
    -nographic\
    -m 4096\
    -smp 2
```

系统启动后,需要配置 SSH 服务以允许远程访问，方便qemu测试使用。

1. 编辑 SSH 配置文件:
```shell
sudo vim /etc/ssh/sshd_config
```

2. 添加或修改以下配置:
```shell
# 允许 root 用户登录
PermitRootLogin yes

# 允许密码认证
PasswordAuthentication yes 

# 允许空密码
PermitEmptyPasswords yes
```

3. 清空 root 用户密码:
```shell
sudo passwd -d root
```

4. 重启 SSH 服务使配置生效:
```shell
sudo systemctl restart sshd
```

现在可以通过以下命令连接到虚拟机:
```shell
ssh root@localhost -p 2222
```