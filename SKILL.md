---
name: "rk3xx-chip-dev"
description: >-
  Rockchip RK3XX 系列芯片（RK3566/RK3568/RK3576/RK3588/RV1106）硬件加速开发技能。
  覆盖 MPP 视频硬编解码、RKNN NPU 推理（单核/双核/三核）、RGA 2D 加速（单/双/三实例）、
  VPU JPEG 硬编、DRM/KMS VOP2 显示、DMA-BUF 全链路零拷贝、ffmpeg-rockchip 硬编解码、
  交叉编译与 glibc 兼容。当用户提到 RK3566/RK3568/RK3576/RK3588/RV1106/RK3XX/Rockchip/
  MPP/RKNN/NPU/RGA/VPU/VOP2/DRM/KMS/DMA-BUF/ION/零拷贝/硬解/硬编/推理模型/rkmpp/rkrga/
  ffmpeg-rockchip/交叉编译/aarch64/armhf/板子部署/SDK sysroot 时激活。
  通用技术（glibc 兼容、环形缓冲 IPC、Go 内存、systemd、安全加固、结构体对齐）见 knowledge/。
allowed-tools: Read, Write, Edit, Bash, Glob, Grep
---

# Rockchip RK3XX 芯片开发技能

> 面向 RK3XX 系列芯片的硬件加速开发技能。聚焦 **RK 芯片专有能力**（MPP/RKNN/RGA/VPU/VOP2/DMA-BUF），
> 通用工程经验（glibc/ringbuf/Go内存/systemd/安全）抽离至 `knowledge/` 知识库。
> 不绑定特定板子 IP/密码/摄像头，通过 `boards/registry.yaml` 参数化配置。

---

## 〇、设计边界（重要）

| 类别 | 位置 | 判定规则 |
|------|------|---------|
| **RK 芯片专有** | `references/` | 依赖 RK SDK 符号/头文件/设备节点（MPP/RKNN/RGA/VPU/VOP2/ffmpeg-rk） |
| **通用技术知识库** | `knowledge/` | 纯语言/OS/架构经验（glibc/ringbuf/Go GC/systemd/安全/对齐），非 RK 特有 |
| **板子能力描述** | `boards/` | 芯片型号 → 硬件能力映射，参数化，用户可扩展 |

**激活后第一步**：读取 `boards/registry.yaml` 确认目标板子，再读对应 `boards/<soc>.md` 获取能力描述。

---

## 一、板子识别与选择

### 1.1 已注册板子

| 板子名 | SoC | 架构 | NPU 核数 | RGA 实例 | VPU JPEG | 编译方式 | 能力文档 |
|--------|-----|------|---------|---------|---------|---------|---------|
| rk3566 | RK3566 | aarch64 | 1 | 1 | ✗ | cross | `boards/rk3566.md` |
| rk3568 | RK3568 | aarch64 | 1 | 1 | ✗ | cross | `boards/rk3568.md` |
| rk3576 | RK3576 | aarch64 | **2** | **2** | ✓ | native/cross | `boards/rk3576.md` |
| rk3588 | RK3588 | aarch64 | **3** | **3** | ✓ | cross | `boards/rk3588.md` |
| rv1106 | RV1106 | armhf | 1 | 1 | ✓ | cross (uclibc) | `boards/rv1106.md` |

### 1.2 配置目标板子

编辑 `boards/registry.yaml`，填入板子 IP/凭据，或用探测脚本自动识别：

```bash
bash boards/probe.sh <ip> [user] [password]
# 输出 SoC 型号 + NPU/RGA/MPP 能力 + SDK 路径，据此选择 boards/<soc>.md
```

### 1.3 芯片能力速查（关键差异点）

| 能力 | RK3566/3568 | RK3576 | RK3588 | RV1106 |
|------|-------------|--------|--------|--------|
| NPU core_mask `CORE_1` | ✗ 无效 | ✓ | ✓ | ✗ |
| NPU 多 context 并行 | ✗ 单核 | ✓ 2 ctx | ✓ 3 ctx | ✗ |
| RGA `im_opt_t.core` | ✗ 单实例 | ✓ 双实例 | ✓ 三实例 | ✗ |
| `mjpeg_rkmpp` 编码器 | ✗ 无 jpege | ✓ | ✓ | ✓ |
| big.LITTLE 绑核 | ✗ 同构 | ✓ A53+A72 | ✓ A55+A76 | ✗ 单核 |
| 板上原生 gcc | 慢 | ✓ 11.4 | ✓ 12 | ✗ 无 |
| RKNN 库 | librknnrt.so | librknnrt.so | librknnrt.so | **librknnmrt.so** |
| 架构 | aarch64 | aarch64 | aarch64 | **armhf** |

---

## 二、激活与触发

当用户请求涉及以下场景时激活：

| 触发词 | 场景 | 首选参考 |
|--------|------|---------|
| RK3566/3568/3576/3588/RV1106/RK3XX | 芯片选型/能力 | `boards/<soc>.md` |
| MPP/硬解/硬编/VDPU/VEPU/rkmpp | 视频编解码 | `references/mpp-api.md` |
| RKNN/NPU/推理/rknn/core_mask | AI 推理 | `references/rknn-api.md` |
| RGA/2D加速/缩放/格式转换/im_opt_t | 图像处理 | `references/rga-api.md` |
| VPU JPEG/mjpeg_rkmpp/JPEG硬编 | JPEG 编码 | `references/vpu-jpeg.md` |
| DRM/HDMI/OSD/VOP2/plane/dumb_buffer | 显示输出 | `references/drm-vop2.md` |
| DMA-BUF/零拷贝/importbuffer_fd/set_io_mem | 零拷贝管线 | `references/dma-buf-zero-copy.md` |
| ffmpeg-rockchip/ffmpeg_rk/rkrga/scale_rkrga | ffmpeg 硬编解码 | `references/ffmpeg-rockchip.md` |
| SDK/sysroot/头文件/库路径/链接策略 | 编译链接 | `references/sdk-sysroot.md` |
| 交叉编译/glibc/GLIBC_2.34/aarch64 | 编译环境 | `knowledge/cross-compile-glibc.md` |
| 环形缓冲/ringbuf/mmap IPC | C/Go 进程通信 | `knowledge/ringbuf-mmap-ipc.md` |
| Go 内存/GC/slice/异步 channel | Go 工程经验 | `knowledge/go-memory-gc.md` |
| systemd/看门狗/开机自启/崩溃重启 | 进程守护 | `knowledge/systemd-watchdog.md` |
| 安全加固/HMAC/路径遍历/strip/防拉取 | 安全防护 | `knowledge/security-hardening.md` |
| 结构体对齐/C/Go FFI/共享内存偏移 | 跨语言内存 | `knowledge/c-go-struct-align.md` |
| 远程调试/SSH/性能分析/OOM | 调试排查 | `knowledge/remote-debugging.md` |

---

## 三、开发工作流

```
┌──────────┐    ┌───────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│  编码    │───>│ 交叉编译  │───>│  部署    │───>│  运行    │───>│  调试    │
│ (编辑器) │    │ (Docker/  │    │ (SCP)    │    │ (SSH)    │    │ (日志)   │
│          │    │  native)  │    │          │    │          │    │          │
└──────────┘    └───────────┘    └──────────┘    └──────────┘    └──────────┘
     ↑                                                                │
     └────────────────────────────────────────────────────────────────┘
```

### 3.1 编译方式选择（按板子能力）

```yaml
# boards/registry.yaml 中 compile_mode 决定编译方式
compile_mode: cross   # Docker 交叉编译（RK3566/3568/3588/RV1106）
compile_mode: native  # 板上原生编译（RK3576 开发阶段）
```

- **cross**: 在 Docker 容器内用 `aarch64-linux-gnu-gcc` 交叉编译，产物 SCP 到板子
- **native**: SSH 连入板子，直接 `g++` 编译（RK3576/RK3588 有板上 gcc 11.4+）
- **生产部署**: 即使板子能原生编译，也推荐交叉编译 + strip，板端不部署源码

### 3.2 链接策略决策树（编译前必走）

```
项目是否需要 RK SDK 库 (MPP / RKNN / RGA)?
├── 否
│   └── 静态链接 (-static / musl)  ← 最简单，零 glibc 依赖
│       ├── C:    aarch64-linux-gnu-gcc -o demo demo.c -static
│       ├── Go:   CGO_ENABLED=1 ... go build -ldflags '-extldflags "-static"'
│       └── Rust: cargo build --release --target aarch64-unknown-linux-musl
│
└── 是 → 需要哪些 SDK 库?
    ├── 仅 librga.a (静态库)
    │   └── 可静态链接: gcc -o demo demo.c -static -lrga
    │
    └── 含 .so 库 (librknnrt / librockchip_mpp)  ← 必须动态链接
        └── 检查容器 glibc vs 板子 glibc:
            ├── 一致 (如 RK3576: 容器 2.35 = 板子 2.35) → 直接动态链接
            └── 不一致 (如 RK3568: 容器 2.35 > 板子 2.31) → 板子 crt 方案
                详见 knowledge/cross-compile-glibc.md
```

### 3.3 部署与运行

```bash
# 从 registry.yaml 读取板子 IP/凭据（以下用 BOARD_IP/BOARD_USER 占位）
scp -o StrictHostKeyChecking=no demo ${BOARD_USER}@${BOARD_IP}:/tmp/
ssh ${BOARD_USER}@${BOARD_IP} "chmod +x /tmp/demo && /tmp/demo"
```

> 完整构建/部署脚本见 `scripts/`，调试命令见 `knowledge/remote-debugging.md`。

---

## 四、SDK 编程速查（RK 芯片专有）

### 4.1 典型管线

```
RTSP H.265 → ffmpeg 解封装 → MPP VDPU 硬解 → NV12
    → RGA (NV12→RGB, resize) → RKNN NPU 推理 → NMS 后处理
```

### 4.2 全链路零拷贝管线（推荐，RK 生态核心优势）

```
ffmpeg hevc_rkmpp 硬解 → AVFrame(DRM PRIME) → desc->objects[0].fd (DMA-BUF fd)
    → RGA importbuffer_fd(mpp_fd) → improcess(缩放) → RKNN input_mem(fd)
    → rknn_set_io_mem + rknn_run  (全程 DMA-BUF 零拷贝, CPU 不参与像素搬运)
```

> 完整代码见 `references/dma-buf-zero-copy.md`，模板见 `templates/cpp-zero-copy/`。

### 4.3 NPU 多核并行（按芯片能力）

```c
// RK3566/3568/RV1106 (单核): 无需 core_mask，默认 CORE_0
rknn_init(&ctx, model, len, 0, NULL);

// RK3576 (双核) / RK3588 (三核): 多 context + 多线程
rknn_context ctx0, ctx1;  // RK3588 再加 ctx2
rknn_init(&ctx0, model, len, 0, NULL);  rknn_set_core_mask(ctx0, RKNN_NPU_CORE_0);
rknn_init(&ctx1, model, len, 0, NULL);  rknn_set_core_mask(ctx1, RKNN_NPU_CORE_1);
// 每个 context 配独立推理线程，处理不同通道子集，无锁竞争
// ⚠️ 错误: 单 context 设 CORE_0_1 无效（Core1 仍 0%）
```

> 完整 API 见 `references/rknn-api.md`，芯片核数见 `boards/<soc>.md`。

### 4.4 RGA 双实例/三实例（按芯片能力）

```c
// RK3576/RK3588: im_opt_t 指定 scheduler（需 C++ 版 improcess 10 参数版）
im_opt_t rga_opt = {};
rga_opt.core = IM_SCHEDULER_RGA2_CORE0;  // = 1<<2 = 4 → scheduler[0]
// 或 IM_SCHEDULER_RGA2_CORE1 = 1<<3 = 8 → scheduler[1]
improcess(src, dst, {}, srect, drect, {0,0,0,0}, -1, NULL, &rga_opt, IM_SYNC);
// RK3566/3568: 单实例，无需 im_opt_t
```

> 完整 API + 陷阱见 `references/rga-api.md`。

### 4.5 VPU JPEG 硬编（RK3576/RK3588/RV1106 专属）

```c
// mjpeg_rkmpp 编码器（VPU 硬件编码，消除 CPU MJPEG 编码）
const AVCodec *enc = avcodec_find_encoder_by_name("mjpeg_rkmpp");
ctx->pix_fmt = AV_PIX_FMT_NV12;       // ⚠️ 必须 NV12，不接受 DRM_PRIME
ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
avcodec_open2(ctx, enc, NULL);
// 流程: mmap DMA-BUF → memcpy NV12 → AVFrame → mjpeg_rkmpp → JPEG
```

> 完整说明见 `references/vpu-jpeg.md`。RK3566/3568 无 jpege 模块，不可用。

### 4.6 DRM/KMS VOP2 显示

```c
// 1. 打开 /dev/dri/card0，枚举 HDMI connector + CRTC
// 2. 自动探测 Smart/Esmart plane（不要硬编码 plane ID，不同芯片不同）
//    - Cluster plane 不支持 LINEAR dumb buffer (SetPlane 返回 -22)，避开
// 3. 视频层: RGA 合成多路 → XRGB8888 dumb buffer → Smart plane (zpos=0)
// 4. OSD 层: ARGB8888 alpha 混合 → Esmart plane (zpos=1)
// 5. OSD 双缓冲: 两个 dumb buffer 交替 SetPlane，消除闪烁
```

> 完整方案见 `references/drm-vop2.md`。

---

## 五、关键陷阱清单（RK 芯片专有，务必遵守）

### MPP 相关

1. **MPP 输入是裸码流**：不含封装信息，需先 ffmpeg 解封装再送 `decode_put_packet`。
2. **MPP split_parse 新 API**：用 `mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1)` + `MPP_DEC_SET_CFG`，替代旧的 `MPP_DEC_SET_PARSER_SPLIT_MODE`。
3. **MPP do-while put/get 循环**：`decode_put_packet` 可能返回 `MPP_ERR_BUFFER_FULL`(-1012)，需先 `decode_get_frame` 取出帧再重试 put。
4. **MPP Info Change 处理**：`split_parse=1` 后 MPP 用内部 buffer group，Info Change 时只需调 `MPP_DEC_SET_INFO_CHANGE_READY`，无需手动配置 buffer 组。
5. **MPP buffer 数量**：H.264/H.265 需 20+ buffer 组（参考帧多），其他格式 10+ 即可。
6. **MPP DMA-BUF fd 零拷贝**：`mpp_buffer_get_fd(buffer)` 获取 fd，直接传给 RGA `importbuffer_fd`，无需 `mpp_buffer_get_ptr` 拷贝。

### RKNN 相关

7. **RKNN 输入格式**：默认 `RKNN_TENSOR_NHWC`（非 NCHW），类型 `UINT8`，RKNN 内部自动归一化。
8. **RKNN 量化类型**：RK35xx/RK3588 系列使用 INT8 量化。
9. **RKNN BGR→RGB**：OpenCV 读取为 BGR，推理前必须 `cvtColor(BGR2RGB)`。
10. **NPU 多核必须多 context**：单 context 设 `CORE_0_1`/`CORE_ALL` 无效，必须 N 个独立 context 分别绑核 + N 个推理线程。
11. **RK3566/3568 NPU 单核**：`core_mask` 仅 `NPU_CORE_AUTO`/`NPU_CORE_0` 有效。
12. **RV1106 库名不同**：`librknnmrt.so`（mrt 后缀），链接 `-lrknnmrt`，非 `-lrknnrt`。
13. **RKNN 模型平台绑定**：`rknn-toolkit2` 转换时指定 `target_platform`，模型不可跨平台使用。

### RGA 相关

14. **RGA stride 对齐**：输入输出 stride 需 16 字节对齐。
15. **RGA imsetColorSpace 陷阱**：调用 `imsetColorSpace` 会导致 `improcess` 失败，不要调用。
16. **RGA IM_STATUS_SUCCESS = 1**：判断成功用 `rs == IM_STATUS_SUCCESS`，不是 `rs == 0`。
17. **RGA wrapbuffer_handle_t stride**：跨设备（NV12 from MPP）时必须用 6 参数版本传入实际 stride，4 参数版本自动算 stride 会出错。
18. **RGA 双实例需 C++ 版**：`im_opt_t` 参数仅 C++ 版 `improcess`（10 参数）有，C 版无此参数。

### VPU JPEG 相关

19. **mjpeg_rkmpp 不接受 DRM_PRIME**：必须 `AV_PIX_FMT_NV12` + 软件帧（mmap DMA-BUF → memcpy → AVFrame），VPU 仍执行硬件编码。
20. **RK3566/3568 无 jpege**：VPU 不含 JPEG 编码模块，`mjpeg_rkmpp` 不可用，只能 CPU 软编 MJPEG。

### ffmpeg-rockchip 相关

21. **ffmpeg extern "C" 保护**：ffmpeg 头文件无 extern "C"，C++ 中必须手动添加 `extern "C" { #include ... }`。
22. **ffmpeg 静态库链接**：直接指定 .a 文件路径，不需要 `-l` 和 `-L`。
23. **ffmpeg DRM PRIME fd 提取**：`AVDRMFrameDescriptor *desc = (AVDRMFrameDescriptor *)frame->data[0]; int fd = desc->objects[0].fd;`。
24. **ffmpeg 命令行限制**：`hwdownload`/`scale_rkrga` 命令行在板上有格式协商问题，零拷贝管线只能用 C API。
25. **rkrga 滤镜需 DRM_PRIME**：`scale_rkrga`/`vpp_rkrga` 只接受 `AV_PIX_FMT_DRM_PRIME` 硬件帧，需 `-init_hw_device rkmpp -hwaccel rkmpp -hwaccel_output_format drm_prime`。

### DRM/VOP2 相关

26. **Cluster plane 不支持 LINEAR**：dumb buffer SetPlane 返回 -22，用 Smart/Esmart plane。
27. **plane ID 不要硬编码**：不同芯片 plane ID 不同（RK3568: 57/129, RK3576: 108/148），代码应自动探测。
28. **OSD 双缓冲消除闪烁**：两个 dumb buffer 交替 SetPlane，避免 CPU 写入时显示撕裂。
29. **SetPlane 源坐标 16.16 定点**：`src_w << 16, src_h << 16`。

### SDK 路径相关

30. **RK3576 ffmpeg 路径不同**：静态库在 `/opt/ffmpeg-build/lib/`（非 `/usr/local/lib/`），头文件在 `/opt/ffmpeg-build/include/`。
31. **RK3576 RKNN 头文件多一层目录**：`/usr/include/rknn/rknn_api.h`（非 `/usr/include/rknn_api.h`）。
32. **SDK tar 用 -h**：从板端打包 SDK 库时必须 `tar -h` 解引用符号链接，否则 `.so` → `.so.1` → `.so.0` 链断裂。

> 通用技术陷阱（glibc/ringbuf/Go 内存/systemd/安全/结构体对齐）见 `knowledge/` 各文档。

---

## 六、参考文档索引

### RK 芯片专有（references/）

| 文档 | 内容 |
|------|------|
| `references/mpp-api.md` | MPP MPI 接口、编解码流程、split_parse 新 API、do-while 循环、Info Change、DMA-BUF fd 零拷贝、错误码 |
| `references/rknn-api.md` | RKNN C/Python API、结构体、core_mask 多核、零拷贝 API、模型转换、预处理/后处理、错误码 |
| `references/rga-api.md` | RGA2 IM2D API、importbuffer_fd 零拷贝、im_opt_t 双实例/三实例、stride 陷阱、格式转换 |
| `references/vpu-jpeg.md` | VPU JPEG 硬编 (mjpeg_rkmpp)、NV12 输入限制、芯片能力差异 |
| `references/drm-vop2.md` | VOP2 plane 探测、DRM dumb buffer、RGA 合成方案、OSD 双缓冲、ARGB8888 alpha 混合 |
| `references/dma-buf-zero-copy.md` | 全链路 DMA-BUF 零拷贝管线（ffmpeg C API → RGA → RKNN）、性能数据 |
| `references/ffmpeg-rockchip.md` | ffmpeg-rk 硬编解码器、rkrga 滤镜、C API 硬解、命令行限制、编译部署 |
| `references/sdk-sysroot.md` | SDK 头文件/库路径、交叉编译链接策略、CMake 工具链、glibc 兼容入口 |

### 通用技术知识库（knowledge/）

| 文档 | 内容 | 备注 |
|------|------|------|
| `knowledge/cross-compile-glibc.md` | glibc 2.31/2.34 兼容、板子 crt 方案、静态 vs 动态链接 | 交叉编译通用，RK 场景高频 |
| `knowledge/ringbuf-mmap-ipc.md` | 环形缓冲 mmap IPC、Poll 背压、C/Go 共享内存 | 通用进程间通信 |
| `knowledge/go-memory-gc.md` | Go slice 引用、GC 调优、异步 channel、快慢路径分离 | 通用 Go 工程经验 |
| `knowledge/systemd-watchdog.md` | systemd 守护、硬件看门狗、dev_deploy 调试模式 | 通用 Linux 服务管理 |
| `knowledge/security-hardening.md` | HMAC、路径遍历、WebSocket Origin、strip、防拉取 | 通用 Web 安全 |
| `knowledge/c-go-struct-align.md` | C/Go 结构体对齐、共享内存偏移、`#pragma pack` | 通用 C/Go FFI |
| `knowledge/remote-debugging.md` | SSH 调试、性能分析、OOM 诊断、NPU/MPP 状态监控 | 通用远程调试 |

### 板子能力描述（boards/）

| 文档 | 内容 |
|------|------|
| `boards/registry.yaml` | 板子注册表（IP/凭据/SoC 映射，用户配置） |
| `boards/<soc>.md` | 各芯片硬件加速器、NPU 核数、RGA 实例、VPU JPEG、SDK 路径、OS/glibc、编译方式 |
| `boards/probe.sh` | 自动探测 SoC 型号 + 能力 |

---

## 七、脚本与模板索引

| 资源 | 说明 |
|------|------|
| `scripts/board_tool.py` | 调试工具（`--board` 参数选板子，sys/npu/mpp/rga/drm/cam/deploy 子命令） |
| `scripts/build_templates.sh` | 一键构建全部模板（容器内执行） |
| `scripts/deploy_run.sh` | 部署到板子并运行 |
| `scripts/quick-build-deploy.sh` | 构建+部署+验证一体化 |
| `docker/` | Dockerfile.base/sdk/board/final + build.sh/build.ps1（参数化板子） |
| `templates/c-static/` | 纯 C 静态链接模板 |
| `templates/c-rknn/` | C + RKNN 推理模板 |
| `templates/cpp-mpp/` | C++ + MPP 解码模板 |
| `templates/cpp-mpp-rknn-rga/` | C++ + MPP + RKNN + RGA 全 SDK 模板 |
| `templates/cpp-zero-copy/` | **全链路零拷贝管线模板**（ffmpeg C API → RGA → RKNN） |
| `templates/cmake-sdk/` | CMake + SDK 模板 |
| `templates/go-cgo-static/` | Go + CGO 静态链接模板 |
| `templates/rust-musl/` | Rust musl 静态链接模板 |

---

## 八、执行准则

1. **先确认板子**：读 `boards/registry.yaml` + 对应 `boards/<soc>.md`，明确 NPU 核数/RGA 实例/VPU JPEG 能力。
2. **编译前走链接决策树**（§3.2），检查容器 vs 板子 glibc 是否一致。
3. **按芯片能力写代码**：单核 NPU 不要设 `core_mask`，单实例 RGA 不要用 `im_opt_t`，无 jpege 不要用 `mjpeg_rkmpp`。
4. **plane ID 自动探测**：不要硬编码 DRM plane ID，不同芯片不同。
5. **零拷贝管线优先**：视频 AI 管线优先用 ffmpeg C API + RGA importbuffer_fd + RKNN set_io_mem（§4.2）。
6. **新建项目从 `templates/` 复制脚手架**：按链接策略选对应模板。
7. **遵守 §五陷阱清单**：尤其 MPP split_parse、do-while 循环、RGA imsetColorSpace、ffmpeg extern "C"、mjpeg_rkmpp NV12 限制。
8. **通用技术查 `knowledge/`**：glibc 兼容、ringbuf、Go 内存、systemd、安全、结构体对齐等非 RK 专有问题。
9. **验证编译结果**：`file demo` 应显示 `ELF 64-bit LSB executable, ARM aarch64`（或 armhf）；动态链接验证 `aarch64-linux-gnu-nm -D demo | grep GLIBC_2`。
10. **板子路径用变量**：脚本中 `${BOARD_IP}`/`${BOARD_USER}` 从 `registry.yaml` 读取，不硬编码。