# input_command 插件

## 简介

`input_command`插件可以通过配置脚本内容，在agent机器上生成可执行的脚本，并通过指定的`cmdpath` 执行该脚本，插件会从脚本执行后`stdout`获得内容进行解析，从而获取脚本内容执行后的信息。

## 版本

[Alpha](../stability-level.md)

## 配置参数

### 基础参数

| 参数                  | 类型       | 是否必选 | 说明                                                                                                                                                      |
|---------------------|----------|------|---------------------------------------------------------------------------------------------------------------------------------------------------------|
| Type                | String   | 是    | 插件类型，指定为`input_command`。                                                                                                                                |
| ScriptType          | String   | 是    | 指定脚本内容的类型，目前支持:bash, shell, python2, python3                                                                                                            |
| User                | String   | 是    | 执行该脚本内容的用户， 不支持root                                                                                                                                     |
| ScriptContent       | String   | 是    | 脚本内容                                                                                                                                                    |
| ContentEncoding     | String   | 否    | 脚本内容的文本格式 <br/> 支持PlainText(纯文本)\|Base64 默认:PlainText                                                                                                   |
| ScriptDataDir       | String   | 否    | 存储脚本的目录路径，脚本在机器上会被存储为文件，指定存储目录时不能包含句号，默认为ilogtail的目录下。                                                                                                  |
| LineSplitSep        | String   | 否    | 脚本输出内容的分隔符，为空时不进行分割                                                                                                                                     |
| CmdPath             | String   | 否    | 执行脚本命令的路径，如果与默认路径不一致，请指定命令路径。默认路径如下：<br/>- bash: /usr/bin/bash<br/>- shell: /usr/bin/sh<br/>- python2: /usr/bin/python2<br/>- python3: /usr/bin/python3 |
| TimeoutMilliSeconds | int      | 否    | 执行脚本的超时时间，单位为毫秒，默认为3000ms                                                                                                                               |
| IntervalMs          | int      | 否    | 采集触发频率，也是脚本执行的频率，单位为毫秒，默认为5000ms                                                                                                                        |
| Environments        | []string | 否    | 环境变量，默认为os.Environ()的值，如果设置了Environments，则在os.Environ()的基础上追加设置的环境变量                                                                                    |

* 采集配置

```yaml
enable: true
inputs:
  - Type: input_command
    User: someone
    ScriptType: shell
    ScriptContent:
        echo -e "test metric commond"
flushers:
  - Type: flusher_stdout
    OnlyStdout: true
```

* 输出

```json
{
    "content":"test metric commond",
    "__time__":"1680079323"
}
```