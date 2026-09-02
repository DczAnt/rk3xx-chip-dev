# Go 内存管理与 GC 调优（通用技术知识库）

> **通用技术**：Go 内存管理是任意 Go 服务的通用工程经验，非 RK 特有。
> 适合高频事件处理、大 buffer 切片、CGO + SQLite 等场景。

## 一、slice 引用底层 array 导致大范围内存无法 GC

```go
// ❌ 错误: ev.Data 引用整个 ringbuf slot (256KB), 即使只需要 35KB JPEG
jpegData := ev.Data[jpegStart:jpegEnd]  // 底层 array 仍是 256KB, GC 无法回收

// ✅ 正确: make + copy 断开引用
jpegData := make([]byte, jpegEnd-jpegStart)
copy(jpegData, ev.Data[jpegStart:jpegEnd])  // 独立 buffer, ev.Data 可被 GC
```

**根因**: Go slice 是 (ptr, len, cap) 三元组，`slice := array[start:end]` 的 ptr 仍指向原 array，只要 slice 存活，整个底层 array 无法 GC。

## 二、无条件 base64 编码 + JSON 序列化

```go
// ❌ 错误: 无 WebSocket 客户端也每事件执行 base64 编码 (~8.8MB/s 分配)
snapMsg, _ := json.Marshal(map[string]interface{}{
    "data": base64.StdEncoding.EncodeToString(jpegData),
})
broadcast(snapMsg)

// ✅ 正确: 先检查是否有客户端, 有才编码
clientsMu.Lock()
hasClients := len(clients) > 0
clientsMu.Unlock()
if hasClients {
    // 才执行 base64 编码 + JSON 序列化
}
```

## 三、GC 调优

```go
import "runtime/debug"

// 大量临时分配 (base64, JSON, JPEG copy) GC 跟不上
debug.SetGCPercent(50)  // 默认 100, 降为 50 更频繁 GC

// 每 3 秒手动 GC 一次
var gcCounter int
func tickHandler() {
    gcCounter++
    if gcCounter >= 30 {  // 30 ticks × 100ms = 3s
        runtime.GC()
        gcCounter = 0
    }
}
```

## 四、快慢路径分离模式（关键架构）

### 问题：SQLite 同步插入阻塞事件循环

SQLite INSERT 耗时 50-60ms/事件，16 事件 = 800-950ms/tick。导致事件循环 tick 间隔 7-31 秒，WebSocket 客户端期间遇不到有效 tick → **收不到画面**。

### 修复：异步 dbWriterLoop + buffered channel

```go
type dbWriteTask struct {
    ev       DetectionEvent
    jpegData []byte
}

func ringReaderLoop(ring *RingReader) {
    dbCh := make(chan dbWriteTask, 256)
    go dbWriterLoop(dbCh)

    for range ticker.C {
        events := ring.Poll()
        for _, ev := range events {
            // 快路径: Poll + JPEG提取 + WebSocket广播 (实时, <10ms)
            // ...

            // 慢路径: 发送到异步 DB writer (非阻塞)
            select {
            case dbCh <- dbWriteTask{ev: ev, jpegData: jpegData}:
            default:
                // 缓冲满时丢弃, 不阻塞快路径
            }
        }
    }
}

func dbWriterLoop(ch <-chan dbWriteTask) {
    for task := range ch {
        // 慢路径: 截图保存 + SQLite插入 + alerter推送 (后台)
        os.WriteFile(snapPath, task.jpegData, 0644)
        db.Exec("INSERT INTO alerts ...")
        alerter.Push(task.ev, task.jpegData)
    }
}
```

### 效果

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| tick 间隔 | 7-31 秒 | ~1 秒 |
| WebSocket snapshot/5s | 0 条 | 4-26 条 |
| WebSocket alert/5s | 0 条 | 12-76 条 |

## 五、告警循环覆盖（保留最近 N 条）

```go
// 每 10 次插入触发清理
if insertCount%10 == 0 {
    db.Exec("DELETE FROM alerts WHERE id NOT IN (SELECT id FROM alerts ORDER BY id DESC LIMIT ?)", maxAlerts)
}
```

## 六、板上 Go 编译注意

```bash
# Go 不在默认 PATH
export PATH=/usr/local/go/bin:/usr/bin:/bin:$PATH
export GOPROXY=https://goproxy.cn,direct  # 国内代理
export CGO_ENABLED=1  # go-sqlite3 需要 CGO

# go-sqlite3 自带 sqlite3 amalgamation 源码, 不需要系统 libsqlite3-dev
go build -o ai_server .
```

## 七、Go build cache 陷阱

```bash
# Docker 交叉编译 go build 可能使用缓存, 新代码未编译进二进制 (文件大小不变)
# 解决: go clean -cache 或 go build -a 强制重新编译
# 验证: strings binary | grep <新字符串> 确认新代码已包含
```

## 八、CGO 交叉编译

```bash
CGO_ENABLED=1 GOOS=linux GOARCH=arm64 \
    CC=aarch64-linux-gnu-gcc \
    CXX=aarch64-linux-gnu-g++ \
    CGO_LDFLAGS="-static" \
    go build -ldflags '-extldflags "-static"' -o demo .
```