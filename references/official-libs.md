# 官方库索引（校准 references 的真实源码来源）

> 本文档映射 6 个 Rockchip 官方库的本地路径、版本、关键头文件，
> 供 agent 在生成/校准代码时**直接读取真实头文件**验证 API 签名，
> 避免凭记忆写错。维护 skill 时优先从此处查证。

## 库清单

| 库 | 本地路径 | 版本 | 用途 |
|----|---------|------|------|
| mpp | `E:\mpp` | 见 `CHANGELOG.md` | MPP 视频硬编解码（VPU） |
| librga | `E:\librga` | **1.10.6** (2026-05-13) | RGA 2D 加速用户态库 |
| RGA_driver | `E:\RGA_driver` | 建议 ≥1.3.13（最低 1.2.4） | RGA 内核驱动 |
| ffmpeg-rockchip | `E:\ffmpeg-rockchip` | 见 `RELEASE`/`RELEASE_NOTES` | ffmpeg 硬编解码 fork |
| rknn-toolkit2 | `E:\Prospace\RAG\RKProjects_lib\rknn-toolkit2-master` | 见 `CHANGELOG.md` | 模型转换工具（PC 端） |
| rknn_model_zoo | `E:\Prospace\RAG\RKProjects_lib\rknn_model_zoo` | — | 27 个现成 RKNN 模型示例 |

## 关键头文件映射

### MPP（`mpp/inc/`）
| 头文件 | 内容 |
|--------|------|
| `rk_mpi.h` | MppApi 结构体：decode/decode_put_packet/decode_get_frame/encode/encode_put_frame/encode_get_packet/poll/dequeue/enqueue/control/reset |
| `rk_mpi_cmd.h` | 控制命令枚举：`MPP_DEC_SET_CFG`(line 114)、`MPP_ENC_SET_CFG` 等 |
| `mpp_buffer.h` | `mpp_buffer_get_fd()` 获取 DMA-BUF fd（零拷贝关键） |
| `mpp_frame.h` | MppFrame 属性：width/height/stride/format |
| `vpu_api.h` | VPU 旧式 API（JPEG 硬编） |
| `mpp_err.h` | 错误码：`MPP_ERR_BUFFER_FULL` = -1012 |

### RGA（`librga/include/`）
| 头文件 | 内容 |
|--------|------|
| `im2d.h` | 总入口，include 所有 im2d_* |
| `im2d_single.h` | 单次操作 API：`imresize`/`imcopy`/`imcrop`/`imcvtcolor`/`imrotate`/`imflip`/`imblend`/`improcess` |
| `im2d_type.h` | `rga_buffer_t`、`im_rect`、`IM_STATUS_SUCCESS`、格式枚举 |
| `im2d_buffer.h` | `wrapbuffer_handle`/`wrapbuffer_virtualaddr`/`importbuffer_fd` |
| `im2d_task.h` | task-API（多操作批量提交） |
| `RgaUtils.h` | 工具函数 |

### RKNN Runtime（`rknpu2/runtime/Linux/librknn_api/`）
| 路径 | 内容 |
|------|------|
| `include/rknn_api.h` | 核心 API：`rknn_init`/`rknn_run`/`rknn_set_core_mask`/`rknn_set_io_mem` |
| `include/rknn_custom_op.h` | **自定义算子** API（skill 未覆盖，高级用法） |
| `include/rknn_matmul_api.h` | **矩阵乘法** API（skill 未覆盖，大模型推理） |
| `aarch64/librknnrt.so` | RK3566/3568/3576/3588 运行时 |
| `armhf/librknnrt.so` | ARM 32 位运行时 |
| `armhf-uclibc/librknnmrt.so` | **RV1106/RV1103** 运行时（uclibc + mrt 后缀） |

### ffmpeg-rockchip（`libavcodec/`）
| 文件 | 内容 |
|------|------|
| `rkmppdec.c` | 硬解解码器实现 |
| `rkmppenc.c` | 硬编编码器实现 |
| `allcodecs.c` | 编解码器注册表（完整列表见下） |

## ffmpeg-rk 完整编解码器列表（来源：`allcodecs.c`）

**解码器**（`*_rkmpp_decoder`）：
h263、h264、hevc、mpeg1、mpeg2、mpeg4、vp8、vp9、**av1**、mjpeg

**编码器**（`*_rkmpp_encoder`）：
h264、hevc、**mjpeg**

> ⚠️ `mjpeg_rkmpp_encoder` 依赖芯片 VPU jpege 模块，RK3566/3568 无此模块。
> 见 `references/vpu-jpeg.md`。

## 版本锁定建议

| 组件 | 推荐版本 | 理由 |
|------|---------|------|
| librga | 1.10.6 | 最新，支持 RK3538/RK3572、CFA、improcessOpt |
| RGA 驱动 | ≥1.3.13 | librga 1.10.6 要求 |
| mpp | 按 BSP，看 `CHANGELOG.md` | 与内核 VPU 驱动配对 |
| rknn-toolkit2 | 按 `CHANGELOG.md` | 与 rknpu2 runtime 版本配对 |

## agent 使用方式

生成/校准 API 调用时，**先读对应头文件**验证签名：
```bash
# 例：验证 imresize 签名
grep "imresize" E:\librga\include\im2d_single.h
# 例：验证 MPP_DEC_SET_CFG 存在
grep "MPP_DEC_SET_CFG" E:\mpp\inc\rk_mpi_cmd.h
```

## 本地路径缺失时的降级（重要）

本地官方库路径**仅用于 skill 维护时的实时再校准**，**非运行依赖**：

| 场景 | 是否需要本地库 | 说明 |
|------|--------------|------|
| agent 用 skill 写代码 | ✗ 不需要 | 依赖 `references/` 内联的已校准 API 信息（v2.0.0 已从真实头文件提取） |
| 交叉编译 | ✗ 不需要 | SDK 在 Docker 镜像内或 `--sdk` 参数挂载，非本地源码路径 |
| skill 升级/再校准 | ✓ 可选 | 路径存在时读真实头文件验证 API 变更；不存在时走降级 |

**降级方案（路径不存在时）**：
1. 依赖 `references/` 文档内联信息（已校准，自包含）
2. 如需再校准，从 GitHub 在线拉取：
   - mpp: `https://github.com/rockchip-linux/mpp`
   - librga: `https://github.com/airockchip/librga`
   - rknn-toolkit2: `https://github.com/airockchip/rknn-toolkit2`
   - ffmpeg-rockchip: `https://github.com/airockchip/FFmpeg`
   - rknn_model_zoo: `https://github.com/airockchip/rknn_model_zoo`
3. 板端 SDK 提取：`ssh root@<board> "tar czf /tmp/sdk.tar.gz -h /usr/lib/librknn*.so /usr/lib/librockchip_mpp*.so /usr/include/rknn_api.h"` → scp 回本地

> **结论**：本地库删除不影响 skill 正常使用，仅影响 skill 维护时的再校准便利性。