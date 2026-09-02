# 远程调试与性能分析（通用技术知识库）

> **通用技术**：SSH 远程调试是任意嵌入式开发的通用方案，非 RK 特有。
> RK 特有的 NPU/MPP/RGA 状态查询命令也收录于此（因属调试范畴）。

## 一、SSH 连接

```bash
# 从 boards/registry.yaml 读取板子 IP/凭据
ssh root@<BOARD_IP>
```

## 二、系统信息

```bash
uname -a
cat /etc/os-release | head -2
cat /proc/cpuinfo | grep -E 'processor|CPU part' | head -8
cat /sys/class/thermal/thermal_zone0/temp  # ÷1000=°C
free -h
df -h
```

## 三、NPU 状态（RK 专有调试命令）

```bash
# NPU 负载 (%)
cat /sys/kernel/debug/rknpu/load

# NPU 驱动版本
cat /sys/kernel/debug/rknpu/driver_version

# NPU 设备
ls /dev/rknpu

# NPU 频率
cat /sys/kernel/debug/rknpu/freq

# 实时监控
watch -n 1 cat /sys/kernel/debug/rknpu/load

# NPU 频率调节
echo performance > /sys/class/devfreq/<npu>/governor  # 高性能模式
```

## 四、MPP 硬件编解码状态（RK 专有）

```bash
ls /dev/mpp_service
strings /usr/lib/aarch64-linux-gnu/librockchip_mpp.so.1 | grep -i version

# VDPU/VEPU/RGA/NPU 中断
cat /proc/interrupts | grep -i 'vdpu\|vepu\|rga\|npu'

# 中断频率 (解码负载)
watch -n 1 "cat /proc/interrupts | grep vdpu"
```

## 五、RGA 状态（RK 专有）

```bash
ls /dev/rga
# RGA 版本
cat /sys/kernel/debug/rkrga/load 2>/dev/null
```

## 六、DRM/GPU

```bash
ls /dev/dri/
cat /sys/class/drm/card0/device/uevent
```

## 七、RTSP 摄像头

```bash
# 探测流信息
ffprobe rtsp://<camera_ip>:554/<path>

# 软解播放
ffplay rtsp://<camera_ip>:554/<path>

# 硬解播放 (ffmpeg 编译了 mpp 支持)
ffplay -c:v hevc_rkmpp rtsp://<camera_ip>:554/<path>

# 录制 5 秒
ffmpeg -i rtsp://<camera_ip>:554/<path> -t 5 -c copy test.mp4

# 抓取一帧
ffmpeg -i rtsp://<camera_ip>:554/<path> -vframes 1 -y snapshot.jpg
```

### 海康摄像头 RTSP URL 格式

```
主码流: rtsp://user:pass@ip:554/Streaming/Channels/101
子码流: rtsp://user:pass@ip:554/Streaming/Channels/102
# 与通用相机 /mpeg4cif 或 /stream1 格式不同
```

## 八、USB 摄像头 (V4L2)

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
v4l2-ctl -d /dev/video0 --get-fmt-video
```

## 九、内核日志

```bash
dmesg | grep -i 'rockchip\|mpp\|vpu\|rga\|npu\|rknpu'
dmesg | tail -50
```

## 十、性能分析（代码内计时）

```c
struct timespec t0, t1;
clock_gettime(CLOCK_MONOTONIC, &t0);
rknn_run(ctx, NULL);  // 或 improcess / decode
clock_gettime(CLOCK_MONOTONIC, &t1);
float ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
printf("elapsed: %.2f ms\n", ms);
```

## 十一、OOM 诊断

```bash
# 检查 OOM Killer 是否杀过进程
dmesg | grep -i "out of memory"
# 典型输出:
# Out of memory: Killed process 5392 (ai_server) total-vm:9851608kB, anon-rss:7577964kB

# 检查当前内存
cat /proc/meminfo | head -5

# 检查进程内存
ps aux | grep <process_name> | grep -v grep
# RSS (第6列) 单位 KB, 除以 1024 得 MB
```

## 十二、文件传输

```bash
# 上传到板子
scp file root@<BOARD_IP>:/tmp/

# 从板子下载
scp root@<BOARD_IP>:/tmp/file .

# 批量同步
rsync -avz ./local_dir/ root@<BOARD_IP>:/tmp/remote_dir/

# 容器内部署 (需 sshpass)
sshpass -p <password> scp -o StrictHostKeyChecking=no file root@<BOARD_IP>:/tmp/
```

## 十三、网络调试

```bash
# WiFi 信号
iwconfig wlan0

# 网络连接
ip addr show wlan0

# 路由表
ip route
```

## 十四、进程管理

```bash
# 停止服务杀孤儿（重要）
pkill -9 -f main_server; pkill -9 -f worker; sleep 2
ps aux | grep -E 'main_server|worker' | grep -v grep
# 若有残留, 手动 kill -9 <PID>
```

## 十五、日志含二进制字符

```bash
# C++ 进程 stdout 含二进制数据, grep 报 "匹配到二进制文件"
# 用 grep -a 强制按文本搜索
grep -a "keyword" /tmp/app.log
```

## 十六、源文件同步

```bash
# 远程开发时先 scp 复制源文件到板, 再编译
# 否则板上 go build / make 用的是旧文件
scp src/main.go root@<BOARD_IP>:/opt/my_app/src/main.go
ssh root@<BOARD_IP> "cd /opt/my_app && go build -o main ."
```

## 十七、系统状态监控数据源

| 指标 | 数据源 | 读取方式 |
|------|--------|---------|
| NPU 利用率 | `/sys/kernel/debug/rknpu/load` | 读文件，解析 "NPU load: XX%" |
| 内存总量 | `/proc/meminfo` MemTotal | 读文件 |
| 内存可用 | `/proc/meminfo` MemAvailable | 读文件 |
| 推理 FPS | ringbuf write_head 增量 / 时间间隔 | (wh_new - wh_old) / delta_t |