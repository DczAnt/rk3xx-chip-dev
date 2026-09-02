# C/Go 结构体对齐与共享内存偏移（通用技术知识库）

> **通用技术**：C/Go FFI 结构体对齐是任意跨语言共享内存的通用问题，非 RK 特有。
> 适合 C 端写入 → Go 端读取的 mmap 共享内存场景。

## 一、问题

C 端 `sizeof(struct)` 与 Go 端硬编码偏移不一致，导致共享内存数据错位。

## 二、示例：CamStats 结构体

```c
// ringbuf.h: 确保结构体实际 40 字节
typedef struct {
    uint32_t fps;            // 4
    uint32_t width;          // 4
    uint32_t height;         // 4
    uint32_t reconnect_cnt;  // 4
    uint32_t bitrate_kbps;   // 4
    uint32_t last_pkt_size;  // 4
    char     codec[16];      // 16
} CamStats;  /* 40 bytes */
// ⚠️ 不要加 _pad 字段！加了会变成 44 字节
```

```go
// Go 端偏移必须与 C 端 sizeof 一致
off := 40 + ch * 40  // RINGBUF_CAMSTATS_OFFSET + ch * sizeof(CamStats)
```

## 三、陷阱：加 _pad 字段导致错位

```c
// ❌ 错误: 加了 _pad
typedef struct {
    uint32_t fps;
    // ... 其他字段
    char codec[16];
    uint32_t _pad;    // ← 多了 4 字节, sizeof 变成 44
} CamStats;
```

```go
// Go 端仍用 off = 40 + ch * 40
// CH1 起每通道错位递增 4 字节, Web 端显示乱码
```

## 四、编译器对齐规则

| 类型 | 对齐要求 | 大小 |
|------|---------|------|
| uint8_t | 1 | 1 |
| uint32_t | 4 | 4 |
| uint64_t | 8 | 8 |
| float (double) | 8 | 8 |
| char[N] | 1 | N |
| struct | 最大成员对齐 | 成员总和 + padding |

```c
// 示例: 编译器可能在成员间插入 padding
typedef struct {
    uint8_t  a;      // 1 byte
    // 3 bytes padding (对齐 uint32_t)
    uint32_t b;      // 4 bytes
} Example;  // sizeof = 8, 不是 5
```

## 五、Go 端硬编码偏移的维护

```go
// 始终用常量定义偏移, 与 C 端 sizeof 对应
const (
    RingbufHeaderSize   = 4096
    CamStatsSize        = 40   // 必须 == sizeof(CamStats)
    CamStatsOffset      = 40   // header 后的 CamStats 数组起始
)

func readCamStats(data []byte, ch int) CamStats {
    off := CamStatsOffset + ch * CamStatsSize
    return CamStats{
        Fps:         binary.LittleEndian.Uint32(data[off:off+4]),
        Width:       binary.LittleEndian.Uint32(data[off+4:off+8]),
        Height:      binary.LittleEndian.Uint32(data[off+8:off+12]),
        // ...
    }
}
```

## 六、修复后必须清理共享内存

```bash
# 修改结构体后必须删除共享内存文件并重启, 否则旧数据按错误偏移读取
rm -f /dev/shm/my_app_ring
systemctl restart my-app
```

或在 systemd service 中配置：
```ini
ExecStartPre=/bin/rm -f /dev/shm/my_app_ring
```

## 七、验证对齐

```c
// C 端: 编译时检查
_Static_assert(sizeof(CamStats) == 40, "CamStats size mismatch");
// 或
printf("sizeof(CamStats) = %zu\n", sizeof(CamStats));  // 应输出 40
```

```go
// Go 端: 运行时检查
if CamStatsSize != 40 { panic("CamStatsSize mismatch") }
```

## 八、float64 跨语言读取

```go
// ❌ 错误: uint64 直接转 float64
val := float64(binary.LittleEndian.Uint64(b[24:32]))

// ✅ 正确: math.Float64frombits
val := math.Float64frombits(binary.LittleEndian.Uint64(b[24:32]))
```

**根因**: Go 的 `float64(uint64)` 是类型转换（数值转换），不是 reinterpret 位模式。C 端写入的 float64 位模式需要 `math.Float64frombits` 还原。