#!/bin/bash
# 板子自动探测脚本：SSH 连入后识别 SoC 型号 + 能力，输出 boards/registry.yaml 片段
# 用法: bash boards/probe.sh <ip> [user] [password]
#   bash boards/probe.sh 192.168.1.100 root <password>
set -e

IP=${1:?Usage: probe.sh <ip> [user] [password]}
USER=${2:-root}
PASS=${3:-}

SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=5"
if [ -n "$PASS" ]; then
    SSH="sshpass -p $PASS ssh $SSH_OPTS $USER@$IP"
else
    SSH="ssh $SSH_OPTS $USER@$IP"
fi

echo "=== Probing board at $IP ==="

# 1. 架构
ARCH=$($SSH "uname -m" 2>/dev/null || echo "UNREACHABLE")
if [ "$ARCH" = "UNREACHABLE" ]; then
    echo "[ERROR] Cannot reach $IP"; exit 1
fi
echo "arch: $ARCH"

# 2. SoC 型号（从 device tree）
SOC=$($SSH "cat /proc/device-tree/model 2>/dev/null || cat /proc/device-tree/compatible 2>/dev/null | tr '\0' '\n' | head -1" 2>/dev/null || echo "unknown")
echo "model: $SOC"

# 3. CPU 核数与架构
$SSH "grep -c processor /proc/cpuinfo; grep -m1 'CPU part' /proc/cpuinfo" 2>/dev/null

# 4. NPU
NPU=$($SSH "ls /dev/rknpu 2>/dev/null && cat /sys/kernel/debug/rknpu/load 2>/dev/null; cat /sys/kernel/debug/rknpu/driver_version 2>/dev/null" 2>/dev/null || echo "NPU: not found")
echo "$NPU"

# 5. RGA
$SSH "ls /dev/rga 2>/dev/null && echo 'RGA: present' || echo 'RGA: not found'" 2>/dev/null

# 6. MPP
$SSH "ls /dev/mpp_service 2>/dev/null && echo 'MPP: present' || echo 'MPP: not found'" 2>/dev/null

# 7. glibc 版本
$SSH "ldd --version 2>/dev/null | head -1" 2>/dev/null

# 8. SDK 库探测
$SSH "find /usr/lib /lib -name 'librknnrt.so' -o -name 'librknnmrt.so' -o -name 'librockchip_mpp.so' -o -name 'librga.so' 2>/dev/null" 2>/dev/null

# 9. ffmpeg-rockchip
$SSH "which ffmpeg 2>/dev/null && ffmpeg -version 2>/dev/null | head -1; ffmpeg -encoders 2>/dev/null | grep rkmpp" 2>/dev/null

# 10. 推断 SoC 系列
case "$SOC" in
    *RK3566*)  echo ">>> SoC: RK3566 (single-core NPU, single RGA)";;
    *RK3568*)  echo ">>> SoC: RK3568 (single-core NPU, single RGA)";;
    *RK3576*)  echo ">>> SoC: RK3576 (dual-core NPU, dual RGA, VPU JPEG)";;
    *RK3588*)  echo ">>> SoC: RK3588 (tri-core NPU, 3x RGA, dual VPU)";;
    *RV1106*)  echo ">>> SoC: RV1106 (armhf, single-core NPU, librknnmrt)";;
    *)         echo ">>> SoC: unknown, check boards/*.md manually";;
esac

echo "=== Probe complete ==="
echo "Fill boards/registry.yaml with the detected IP/credentials."