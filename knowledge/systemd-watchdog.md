# systemd 进程守护 + 硬件看门狗（通用技术知识库）

> **通用技术**：systemd 服务管理 + 硬件看门狗是任意 Linux 嵌入式部署的通用方案，非 RK 特有。

## 一、三级守护链路

```
systemd (PID 1)
  └── my-app.service (主进程, 如 Go 服务器)
       └── worker (子进程, 如 C++ 检测管线)

硬件看门狗 (/dev/watchdog0) ← systemd 喂狗
```

| 故障场景 | 检测机制 | 恢复动作 | 延迟 |
|---------|---------|---------|------|
| worker 崩溃 | heartbeat 15s 无更新 | 主进程 Restart | ~15s |
| 主进程崩溃 | systemd 进程退出 | Restart=always | 5s |
| 系统挂死 | 硬件看门狗 30s | 硬件重启 | 30s |
| 开机 | systemd enabled | 自动启动 | - |

## 二、systemd service 文件

```ini
[Unit]
Description=My AI Application
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/my_app
EnvironmentFile=/opt/my_app/config/service.env
ExecStartPre=/bin/rm -f /dev/shm/my_app_ring       # 清理残留共享内存
ExecStart=/opt/my_app/main_server
Restart=always
RestartSec=5
StartLimitInterval=300
StartLimitBurst=10
KillMode=mixed
KillSignal=SIGTERM
TimeoutStopSec=10
StandardOutput=journal
StandardError=journal
SyslogIdentifier=my-app

[Install]
WantedBy=multi-user.target
```

### 关键配置说明

- `ExecStartPre=/bin/rm -f /dev/shm/my_app_ring` — 清理残留共享内存（上次崩溃残留的 shm magic 匹配但 write_head/read_tail 过期会导致数据错乱）
- `After=network-online.target` — 等网络就绪（RTSP 依赖）
- `StartLimitBurst=10/300s` — 5 分钟内崩溃超 10 次停止重启
- `KillMode=mixed` — SIGTERM 给主进程，SIGKILL 给整个 cgroup
- `EnvironmentFile` — 加载密钥等环境变量（权限 600）

## 三、硬件看门狗

```bash
# 启用 systemd 硬件看门狗（喂狗周期 30s）
sed -i 's/^#RuntimeWatchdogSec=.*/RuntimeWatchdogSec=30/' /etc/systemd/system.conf
# 或追加
echo "RuntimeWatchdogSec=30" >> /etc/systemd/system.conf
```

systemd 每 15s 喂 `/dev/watchdog0`。系统挂死 30s 后硬件自动重启。

> **注意**: `RuntimeWatchdogSec` 设在 `/etc/systemd/system.conf`（**非** service 文件），systemd 全局喂狗。

## 四、孤儿进程清理

```go
// 扫描 /proc, 清理所有残留的 worker 进程
// 用 /proc/<pid>/comm 精确匹配进程名, 避免误杀
func cleanupOrphanWorkers() {
    entries, _ := os.ReadDir("/proc")
    for _, e := range entries {
        data, _ := os.ReadFile(filepath.Join("/proc", e.Name(), "comm"))
        if strings.TrimSpace(string(data)) == "worker" {
            pid, _ := strconv.Atoi(e.Name())
            syscall.Kill(pid, syscall.SIGTERM)
            time.Sleep(500 * time.Millisecond)
            if syscall.Kill(pid, 0) == nil {
                syscall.Kill(pid, syscall.SIGKILL)
            }
        }
    }
}
```

> **重要**: `pkill main_server` 后 worker 变孤儿可能继续持有资源（如 DRM plane），必须 `pkill -9 -f worker` + `ps` 确认无残留。

## 五、调试部署管理（dev_deploy.sh）

### 问题

systemd `Restart=always` 在调试时自动重启进程：
```
kill -9 main_server → systemd 6 秒内重启旧版本 → 新二进制无法覆盖 (文件占用)
```

### 解决方案

`dev_deploy.sh`（板端运行，通用可复用）：

| 命令 | 作用 |
|------|------|
| `stop` | 停服务 + 禁用自启（调试模式，板子重启也不会自动拉起） |
| `start` | 启用自启 + 启服务（生产模式） |
| `deploy` | 一键：停 → 等待 → 启 |
| `status` | 服务/进程/API/内存/NPU 状态 |
| `run-server` | 手动前台运行主进程（调试） |
| `run-worker` | 手动前台运行 worker（调试） |
| `logs` | 最近 50 行日志 |
| `restart` | 重启服务 |

### 典型流程

```bash
# Windows 侧快速迭代（改 Go 代码后最常用）
ssh root@BOARD_IP "/opt/my_app/deploy/dev_deploy.sh stop"
scp main_server root@BOARD_IP:/opt/my_app/
ssh root@BOARD_IP "/opt/my_app/deploy/dev_deploy.sh start"

# 手动前台调试（需要看实时输出）
ssh root@BOARD_IP
cd /opt/my_app
./deploy/dev_deploy.sh stop
./main_server    # 前台, Ctrl+C 退出
./deploy/dev_deploy.sh start  # 调试完恢复
```

### 通用化

脚本顶部可配置参数：
```bash
INSTALL_DIR="/opt/my_app"        # 安装目录
SERVICE_NAME="my-app"             # systemd 服务名
WEB_PORT=8080                     # API 端口
```
适配其他项目：修改这 3 个变量 + 进程名匹配即可。

## 六、scp 部署前先停服务

```bash
# scp 覆盖运行中二进制会失败 (dest open: Failure)
# 必须先停服务 + pkill 清理进程, 再 scp, 最后启动
ssh root@BOARD_IP "systemctl stop my-app; pkill -9 -f worker"
scp main_server root@BOARD_IP:/opt/my_app/
ssh root@BOARD_IP "systemctl start my-app"
```

## 七、调试与稳定性测试互斥

稳定性测试会 kill 进程 + iptables 断网，与调试部署同时执行会互相干扰。**调试完成后再运行测试**。