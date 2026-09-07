# SSH 免密登录配置（研发调试前置）

> 通用技术，非 RK 专有。但**板子调试前必须配置**，否则每次 SSH/SCP 都需密码，
> 脚本自动化受阻，且明文密码经 sshpass 传递有泄露风险。

## 为什么免密

| 方式 | 安全 | 便捷 | 脚本自动化 | 适用 |
|------|------|------|-----------|------|
| sshpass -p 明文 | ✗ 密码在进程列表/日志可见 | 一般 | ✓ | 临时调试 |
| **SSH key 免密** | ✓ 私钥不离开本机 | ✓ 无需输密码 | ✓ | **研发调试首选** |
| plink -pw 明文 | ✗ | 一般 | ✓ | Windows 临时 |

## 配置步骤

### Linux / macOS / Windows(OpenSSH)

```bash
# 1. 生成密钥对（若已有 ~/.ssh/id_rsa 可跳过）
ssh-keygen -t rsa -b 4096 -f ~/.ssh/id_rsa -N ""  # -N "" 空密码，自动化友好

# 2. 推送公钥到板子
ssh-copy-id -i ~/.ssh/id_rsa.pub root@<BOARD_IP>
# 或手动（板子无 ssh-copy-id 时）：
cat ~/.ssh/id_rsa.pub | ssh root@<BOARD_IP> "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys"

# 3. 验证免密（不应提示密码）
ssh -o BatchMode=yes root@<BOARD_IP> "echo ok"
# 输出 "ok" = 免密成功；提示密码/报错 = 未配置
```

### Windows（无 ssh-copy-id）

```powershell
# 1. 生成密钥
ssh-keygen -t rsa -b 4096 -f $env:USERPROFILE\.ssh\id_rsa -N ""

# 2. 推送公钥（Windows 无 ssh-copy-id，用 type + ssh）
type $env:USERPROFILE\.ssh\id_rsa.pub | ssh root@<BOARD_IP> "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"

# 3. 验证
ssh -o BatchMode=yes root@<BOARD_IP> "echo ok"
```

### 在 registry.yaml 启用

```yaml
boards:
  rk3568:
    ssh_user: root
    ssh_password: ""      # 留空
    ssh_key: ~/.ssh/id_rsa  # 启用 key 认证（board_tool.py 优先用 key）
```

## 故障排查

| 症状 | 原因 | 修复 |
|------|------|------|
| 仍提示密码 | 公钥未追加 / authorized_keys 权限错 | `chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys` |
| Permission denied | 私钥权限过宽 | `chmod 600 ~/.ssh/id_rsa` |
| PubkeyAuthentication disabled | sshd_config 禁了公钥 | 板上 `/etc/ssh/sshd_config` 设 `PubkeyAuthentication yes`，重启 sshd |
| SELinux 拒绝 | 家目录标签错 | `restorecon -R ~/.ssh` |
| Windows: could not open file | 路径含空格 | 用 `"$env:USERPROFILE\.ssh\id_rsa"` 引号 |

## 验证（skill 内置）

```bash
# diagnose 子命令自动检测免密状态
python scripts/board_tool.py --board rk3568 --ip <ip> diagnose
# 输出 "SSH 免密: OK" 或 "MISSING" + 配置指引
```

## 与 sshpass 的关系

- **免密已配置**：board_tool.py 优先用 key，不传密码
- **免密未配置但有密码**：回退 sshpass/plink（向后兼容）
- **都没有**：尝试默认免密（ssh-agent 或默认 key），失败报错