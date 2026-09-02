# 安全加固（通用技术知识库）

> **通用技术**：Web 安全加固是任意嵌入式 Web 服务的通用方案，非 RK 特有。

## 一、HMAC Session 认证

```go
// 密钥来源：环境变量 > 随机生成
if key := os.Getenv("AUTH_HMAC_KEY"); key != "" {
    authHmacKey = []byte(key)           // 持久化密钥
} else {
    rand.Read(authHmacKey)              // 随机密钥（重启后 session 失效）
}

// Session 格式: user|expiry|HMAC(user|expiry)
// Cookie: HttpOnly + SameSite=Lax + 24h TTL
```

**关键**：生产环境必须设置 `AUTH_HMAC_KEY` 环境变量（通过 systemd EnvironmentFile），否则每次服务重启所有用户 session 失效。

## 二、路径遍历防护

```go
// 模型/文件上传时清洗文件名
filename := filepath.Base(handler.Filename)  // 去除 ../ 等路径遍历
```

## 三、WebSocket Origin 校验

```go
upgrader = websocket.Upgrader{CheckOrigin: func(r *http.Request) bool {
    h := r.Host
    return h == "localhost:8080" || strings.HasPrefix(h, "192.168.3.")
}}
```

## 四、代码防拉取（生产交付）

| 措施 | 说明 | 优先级 |
|------|------|--------|
| 不部署源码 | 板端只放二进制 + web + config + models | P0 |
| 二进制 strip | Go: `-ldflags="-s -w"`, C++: `strip` | P0 |
| HMAC 密钥持久化 | systemd EnvironmentFile | P0 |
| 路径遍历防护 | `filepath.Base()` 清洗 | P0 |
| WebSocket Origin | CheckOrigin 校验来源 IP | P0 |
| 目录权限 700 | `chmod 700 /opt/my_app` | P1 |
| SSH 密钥登录 | 禁用密码认证，改强密码 | P0 |
| 禁用 SCP/SFTP | 注释 `Subsystem sftp` | P1 |
| 防火墙收口 | 仅暴露 8080 + 22(白名单) | P1 |
| 硬件看门狗 | 系统挂死自动重启 | P1 |
| Secure Boot | 签名固件，防篡改 | P2 |

### 二进制 strip 效果

```bash
# Go: -ldflags="-s -w" 去符号表+调试信息
go build -ldflags="-s -w" -o ai_server .   # 14M → 8.4M

# C++: strip
aarch64-linux-gnu-strip ai_monitor          # 18M → 17M
```

## 五、验证：strings 检查编译结果

```bash
# 确认新代码已编译进二进制, 排除 include 路径陷阱 / Go build cache 问题
strings binary | grep <新字符串>
# 文件大小和时间戳变化不代表新代码已包含
```

## 六、include 路径陷阱

```bash
# scp 误将头文件复制到 src/ 目录会导致编译器优先使用旧副本
# (#include "x.h" 先搜当前目录)
# 始终用 strings binary | grep <新字符串> 验证编译结果
```