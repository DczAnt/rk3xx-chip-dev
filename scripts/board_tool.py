#!/usr/bin/env python3
"""
board_tool.py — Parameterized board operations tool for RK3XX series.

Replaces the original rk3568_tool.py which hardcoded 192.168.3.208.
All board-specific values come from boards/registry.yaml.

Usage:
    python board_tool.py --board rk3576 --ip 192.168.1.100 probe
    python board_tool.py --board rk3576 --ip 192.168.1.100 --user root deploy ./app
    python board_tool.py --board rk3576 --ip 192.168.1.100 exec "cat /proc/cpuinfo"
    python board_tool.py --board rk3576 --ip 192.168.1.100 reboot

Requires: PyYAML, and sshpass (Linux) or plink (Windows) for non-interactive SSH.
See AGENTS.md for multi-agent tool compatibility notes.
"""
import argparse
import os
import sys
import subprocess
import shutil
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("PyYAML required: pip install pyyaml")


def load_registry(registry_path: Path) -> dict:
    if not registry_path.exists():
        sys.exit(f"registry.yaml not found: {registry_path}")
    with open(registry_path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def get_board_info(registry: dict, board: str) -> dict:
    boards = registry.get("boards", {})
    if board not in boards:
        sys.exit(f"unknown board '{board}'. Available: {list(boards.keys())}")
    return boards[board]


def ssh_cmd(ip: str, user: str, password: str, remote_cmd: str, port: int = 22) -> list:
    """Build SSH command, auto-detecting sshpass (Linux) or plink (Windows)."""
    if shutil.which("sshpass"):
        return ["sshpass", "-p", password,
                "ssh", "-o", "StrictHostKeyChecking=no", "-p", str(port),
                f"{user}@{ip}", remote_cmd]
    elif shutil.which("plink"):
        return ["plink", "-batch", "-pw", password, "-P", str(port),
                f"{user}@{ip}", remote_cmd]
    else:
        sys.exit("need sshpass (Linux) or plink (Windows) for non-interactive SSH")


def scp_cmd(ip: str, user: str, password: str, local: str, remote: str,
            port: int = 22) -> list:
    if shutil.which("sshpass"):
        return ["sshpass", "-p", password,
                "scp", "-o", "StrictHostKeyChecking=no", "-P", str(port),
                local, f"{user}@{ip}:{remote}"]
    elif shutil.which("pscp"):
        return ["pscp", "-batch", "-pw", password, "-P", str(port),
                local, f"{user}@{ip}:{remote}"]
    else:
        sys.exit("need sshpass/pscp for non-interactive SCP")


def cmd_probe(args, board_info):
    """Probe board: SSH in, read /proc/device-tree/compatible, check NPU/MPP."""
    ip, user, pw = args.ip, args.user, args.password or board_info.get("password", "")
    if not pw:
        sys.exit("password required (--password or in registry.yaml)")
    cmds = [
        "cat /proc/device-tree/compatible",
        "cat /proc/cpuinfo | grep -c ^processor",
        "ls /usr/lib/librknnrt.so /usr/lib/librknnmrt.so 2>/dev/null",
        "ls /usr/lib/librockchip_mpp.so 2>/dev/null",
        "ls /usr/lib/librga.so 2>/dev/null",
        "uname -m",
    ]
    for c in cmds:
        print(f"$ {c}")
        r = subprocess.run(ssh_cmd(ip, user, pw, c), capture_output=True, text=True)
        print(r.stdout.strip() or r.stderr.strip())
    print(f"\nExpected SoC: {board_info.get('soc', 'unknown')}")
    print(f"Expected arch: {board_info.get('arch', 'unknown')}")


def cmd_deploy(args, board_info):
    """Deploy local binary to board."""
    ip, user, pw = args.ip, args.user, args.password or board_info.get("password", "")
    local = args.local
    remote = args.remote or f"/tmp/{os.path.basename(local)}"
    print(f"deploying {local} -> {user}@{ip}:{remote}")
    subprocess.run(scp_cmd(ip, user, pw, local, remote), check=True)
    subprocess.run(ssh_cmd(ip, user, pw, f"chmod +x {remote}"), check=True)
    print(f"deployed. run: {user}@{ip} {remote}")


def cmd_exec(args, board_info):
    """Execute remote command."""
    ip, user, pw = args.ip, args.user, args.password or board_info.get("password", "")
    subprocess.run(ssh_cmd(ip, user, pw, args.remote_cmd))


def cmd_reboot(args, board_info):
    """Reboot board."""
    ip, user, pw = args.ip, args.user, args.password or board_info.get("password", "")
    if not args.yes:
        ans = input(f"reboot {user}@{ip}? [y/N] ")
        if ans.lower() != "y":
            return
    subprocess.run(ssh_cmd(ip, user, pw, "reboot"))


def cmd_diagnose(args, board_info):
    """Full environment diagnosis: board + dev env + matching + missing impact."""
    ip, user, pw = args.ip, args.user, args.password or board_info.get("password", "")
    if not pw:
        sys.exit("password required (--password or in registry.yaml)")
    expected_soc = board_info.get("soc", "unknown")
    expected_arch = board_info.get("arch", "unknown")

    def ssh_run(cmd):
        r = subprocess.run(ssh_cmd(ip, user, pw, cmd), capture_output=True, text=True)
        return (r.stdout or r.stderr).strip()

    def ssh_check(cmd):
        return subprocess.run(ssh_cmd(ip, user, pw, cmd),
                              capture_output=True).returncode == 0

    def mark(ok):
        return "OK" if ok else "MISSING"

    print("=" * 64)
    print(f"环境诊断: {args.board} (期望 {expected_soc}/{expected_arch}) @ {ip}")
    print("=" * 64)

    # --- 1. 板端基本信息 + 匹配 ---
    print("\n[1] 板端信息与匹配")
    actual_arch = ssh_run("uname -m")
    soc_model = ssh_run("cat /proc/device-tree/model 2>/dev/null")
    glibc_ver = ssh_run("ldd --version 2>/dev/null | head -1")
    print(f"  SoC 型号  : {soc_model}")
    print(f"  架构      : {actual_arch} (期望 {expected_arch})")
    print(f"  glibc     : {glibc_ver}")
    soc_match = expected_soc.lower() in soc_model.lower()
    arch_ok = (actual_arch == "aarch64" and expected_arch == "aarch64") or \
              (actual_arch( == "armv7l" and expected_arch == "armhf")
    if not soc_match:
        print(f"  [!] SoC 不匹配: 期望 {expected_soc}，实际 {soc_model}")
    if not arch_ok:
        print(f"  [!] 架构不匹配: 期望 {expected_arch}，实际 {actual_arch}")

    # --- 2. 板端 SDK 库 ---
    print("\n[2] 板端 SDK 库")
    libs = [
        ("librknnrt.so", "NPU 推理", "从 rknpu2/runtime/Linux/librknn_api/<arch>/ 部署"),
        ("librockchip_mpp.so", "视频硬编解码", "装 rockchip-mpp 包"),
        ("librga.so", "2D 加速", "装 librga 包"),
        ("librockchip_mpp_v2.so", "MPP v2 API", "更新 mpp"),
    ]
    for lib, purpose, fix in libs:
        ok = ssh_check(f"ls /usr/lib/{lib} 2>/dev/null")
        print(f"  {lib:30s} {mark(ok)}  ({purpose})")
        if not ok:
            print(f"    影响: {purpose}不可用 → 修复: {fix}")

    # --- 3. 板端工具（基于实测教训）---
    print("\n[3] 板端工具")
    tools = [
        ("gcc", "原生编译", "交叉编译替代"),
        ("python3", "板端 Python 脚本", "scp 到本地跑"),
        ("curl", "HTTP 测试", "改用 wget 或 python urllib"),
        ("wget", "下载", "改用 curl 或 python urllib"),
        ("sqlite3", "DB CLI 查询", "用 python3 -c 'import sqlite3'"),
        ("mosquitto_pub", "MQTT 发布测试", "自写 python paho-mqtt"),
        ("ffmpeg", "ffmpeg 命令行", "用 C API 或装 ffmpeg-rockchip"),
    ]
    for tool, purpose, fix in tools:
        ok = ssh_check(f"which {tool} 2>/dev/null")
        print(f"  {tool:16s} {mark(ok)}  ({purpose})")
        if not ok:
            print(f"    降级: {fix}")
    fd_limit = ssh_run("ulimit -n")
    print(f"  fd 软上限      : {fd_limit}  (<1024 多 fd 场景须 ulimit -n 提升)")

    # --- 4. 开发环境（本机）---
    print("\n[4] 开发环境 (本机)")
    dev_tools = [
        ("aarch64-linux-gnu-gcc", "aarch64 交叉编译", "用 Docker 镜像内编译器"),
        ("arm-linux-gnueabihf-gcc", "armhf 交叉编译 (RV1106)", "用 Docker 镜像内编译器"),
        ("docker", "容器化交叉编译", "本机装交叉编译器"),
        ("sshpass", "非交互 SSH (Linux)", "改用 plink (Windows)"),
        ("plink", "非交互 SSH (Windows)", "改用 sshpass (Linux)"),
    ]
    for tool, purpose, fix in dev_tools:
        ok = bool(shutil.which(tool))
        print(f"  {tool:28s} {mark(ok)}  ({purpose})")
        if not ok:
            print(f"    降级: {fix}")
    print(f"  python3-yaml              OK  (已加载)")

    # --- 5. 官方库本地路径（可选）---
    if os.name == "nt":
        print("\n[5] 官方库本地路径 (可选，仅维护时再校准)")
        lib_paths = {
            "mpp": r"E:\mpp",
            "librga": r"E:\librga",
            "ffmpeg-rockchip": r"E:\ffmpeg-rockchip",
            "rknn-toolkit2": r"E:\Prospace\RAG\RKProjects_lib\rknn-toolkit2-master",
        }
        for name, path in lib_paths.items():
            exists = Path(path).exists()
            print(f"  {name:20s} {mark(exists)}  ({path})")
        if not all(Path(p).exists() for p in lib_paths.values()):
            print("    路径缺失不影响 skill 使用，见 references/official-libs.md 降级方案")

    print("\n" + "=" * 64)
    print("诊断完成。MISSING 项需处理，见对应"影响/降级"说明。")
    print("=" * 64)


def main():
    p = argparse.ArgumentParser(description="RK3XX board tool (parameterized)")
    p.add_argument("--board", required=True, help="board name from registry.yaml")
    p.add_argument("--ip", required=True, help="board IP address")
    p.add_argument("--user", default="root", help="SSH user")
    p.add_argument("--password", default=None, help="SSH password (or set in registry)")
    p.add_argument("--port", type=int, default=22, help="SSH port")
    p.add_argument("--registry", default=None, help="path to registry.yaml")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("probe", help="detect SoC and check RK libs")
    d = sub.add_parser("deploy", help="scp binary to board")
    d.add_argument("local", help="local file path")
    d.add_argument("--remote", default=None, help="remote path (default /tmp/<name>)")
    e = sub.add_parser("exec", help="run remote command")
    e.add_argument("remote_cmd", help="shell command")
    r = sub.add_parser("reboot", help="reboot board")
    r.add_argument("-y", "--yes", action="store_true", help="skip confirm")
    sub.add_parser("diagnose", help="full env diagnosis: board + dev + matching + missing impact")

    args = p.parse_args()
    reg_path = Path(args.registry) if args.registry else \
               Path(__file__).parent.parent / "boards" / "registry.yaml"
    registry = load_registry(reg_path)
    board_info = get_board_info(registry, args.board)

    {"probe": cmd_probe, "deploy": cmd_deploy,
     "exec": cmd_exec, "reboot": cmd_reboot,
     "diagnose": cmd_diagnose}[args.cmd](args, board_info)


if __name__ == "__main__":
    main()