# 环形缓冲 mmap IPC（通用技术知识库）

> **通用技术**：mmap 共享内存环形缓冲是任意 C/C++ → Go 进程间零拷贝通信的通用方案，非 RK 特有。
> 适合高频事件传递（如 AI 检测结果 C++ 端 → Go Web 服务）。

## 一、协议设计

### 内存布局

```
┌─────────────────────────────────────────────────────────┐
│ Header (4096 bytes)                                      │
│   [0:4]    magic    = 0x52494E47 ("RING")                │
│   [4:8]    version  = 1                                  │
│   [8:12]   slot_count = 128                              │
│   [12:16]  slot_size  = 262144 (256KB)                   │
│   [16:24]  write_head (uint64, C++写)                    │
│   [24:32]  read_tail  (uint64, Go写)                     │
│   [32:4096] reserved                                   │
├─────────────────────────────────────────────────────────┤
│ Slot 0 (256KB)                                           │
│   [0:8]   timestamp_ms (uint64)                          │
│   [8]     channel (uint8)                                │
│   [9:12]  _pad (3 bytes)                                 │
│   [12:16] person_count (uint32)                          │
│   [16:20] bbox_length (uint32)                           │
│   [20:24] snapshot_length (uint32)                       │
│   [24:32] avg_rga_ms (float64)                           │
│   [32:40] avg_npu_ms (float64)                           │
│   [40:]   data[bbox_length + snapshot_length]            │
├─────────────────────────────────────────────────────────┤
│ Slot 1 ... Slot 127                                      │
└─────────────────────────────────────────────────────────┘
```

### 关键偏移量（易错点）

| 字段 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| write_head | 16 | 8 | C++ 写入，Go 读取 |
| read_tail | 24 | 8 | Go 写入，C++ 读取 |
| 事件数据起始 | 40 | - | **不是 64**（sizeof(RingBufEvent) 有编译器对齐） |
| 总大小 | 4096 + 128×262144 | 33MB | - |

## 二、C++ 写入侧

```cpp
#include <sys/mman.h>
#include <fcntl.h>

int fd = shm_open("/ai_video_ring", O_CREAT|O_RDWR, 0666);
ftruncate(fd, total_size);
uint8_t *p = (uint8_t*)mmap(NULL, total_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

uint64_t idx = __atomic_fetch_add(&meta->write_head, 1, __ATOMIC_RELAXED);
uint8_t *slot = p + 4096 + (idx % 128) * 262144;
*(uint64_t*)(slot+0)  = timestamp_ms;
slot[8] = channel;
*(uint32_t*)(slot+12) = person_count;
*(uint32_t*)(slot+16) = bbox_length;
*(uint32_t*)(slot+20) = snapshot_length;
*(double*)(slot+24)   = avg_rga_ms;
*(double*)(slot+32)   = avg_npu_ms;
memcpy(slot+40, bbox_json, bbox_length);
memcpy(slot+40+bbox_length, jpeg_data, snapshot_length);
```

## 三、Go 读取侧

```go
import ("syscall"; "encoding/binary"; "math"; "unsafe")

f, _ := os.OpenFile("/dev/shm/ai_video_ring", os.O_RDWR, 0)
data, _ := syscall.Mmap(int(f.Fd()), 0, totalSize,
    syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_SHARED)

writeHead := binary.LittleEndian.Uint64(data[16:])
readTail := binary.LittleEndian.Uint64(data[24:])

for idx := readTail; idx < writeHead; idx++ {
    off := 4096 + int(idx%128)*262144
    b := data[off:]
    ev := DetectionEvent{
        TimestampMs:    binary.LittleEndian.Uint64(b[0:8]),
        Channel:        b[8],
        PersonCount:    binary.LittleEndian.Uint32(b[12:16]),
        AvgRgaMs:       math.Float64frombits(binary.LittleEndian.Uint64(b[24:32])),  // ⚠️ 不是 uint64 直接转换
        AvgNpuMs:       math.Float64frombits(binary.LittleEndian.Uint64(b[32:40])),
    }
    ev.Data = make([]byte, ev.BboxLength + ev.SnapshotLength)
    copy(ev.Data, b[40:40+ev.BboxLength+ev.SnapshotLength])
}
binary.LittleEndian.PutUint64(data[24:], newReadTail)
```

## 四、已验证的关键 Bug

| Bug | 错误做法 | 正确做法 | 原因 |
|-----|---------|---------|------|
| write_head 偏移 | 读取 data[4064:] | 读取 data[16:] | meta 结构体有 padding，但 write_head 在偏移 16 |
| 事件数据偏移 | 从 slot+64 开始 | 从 slot+40 开始 | C++ 结构体对齐到 40 字节，非 64 |
| float64 读取 | uint64 直接转 float64 | `math.Float64frombits(uint64)` | Go 类型转换不 reinterpret 位模式 |
| mmap 权限 | 只用 PROT_READ | 需 PROT_READ\|PROT_WRITE (写 read_tail) | - |

## 五、Poll() 背压策略（关键修复）

### 问题：消费者慢于生产者时内存爆炸

当 Go 消费者因 SQLite I/O 等原因变慢，`write_head - read_tail` 积压可达数万。若 Poll() 一次性读取全部积压事件，每事件 ~41KB，44968 事件 = **1.8GB/次** 分配 → OOM Killer 杀进程。

### 修复：限制每次最多读取 16 事件，跳过旧事件

```go
func (r *RingReader) Poll() []DetectionEvent {
    writeHead := binary.LittleEndian.Uint64(r.data[16:])
    readTail := binary.LittleEndian.Uint64(r.data[24:])
    if writeHead <= readTail { return nil }

    maxEvents := uint64(16)
    if writeHead-readTail > maxEvents {
        readTail = writeHead - maxEvents  // 跳过旧事件
    }

    var events []DetectionEvent
    for idx := readTail; idx < writeHead; idx++ {
        off := 4096 + int(idx%ringSlotCount)*ringSlotSize
        ev := r.parseEvent(off)
        if ev != nil { events = append(events, *ev) }
        r.offset = idx + 1
    }
    binary.LittleEndian.PutUint64(r.data[24:], r.offset)
    return events
}
```

### 效果

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| write_head - read_tail | 44968 | 0-225 |
| 每次 Poll() 分配 | 1.8GB | ~656KB |
| 进程内存 | 7.6GB (OOM) | ~400MB |

## 六、性能数据

| 指标 | 值 | 说明 |
|------|-----|------|
| 总内存 | 33MB | 4096 + 128×256KB |
| 事件延迟 | <1ms | mmap 共享内存，无 IPC 调用 |
| 吞吐量 | 60 事件/秒 | 15fps×4ch |
| 快照大小 | 640×360 q80 ≈ 27KB | 单 slot 256KB 足够 |
| Poll() 最大事件数 | 16 | 背压策略 |