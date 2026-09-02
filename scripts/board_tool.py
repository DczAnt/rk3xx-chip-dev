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

    args = p.parse_args()
    reg_path = Path(args.registry) if args.registry else \
               Path(__file__).parent.parent / "boards" / "registry.yaml"
    registry = load_registry(reg_path)
    board_info = get_board_info(registry, args.board)

    {"probe": cmd_probe, "deploy": cmd_deploy,
     "exec": cmd_exec, "reboot": cmd_reboot}[args.cmd](args, board_info)


if __name__ == "__main__":
    main()