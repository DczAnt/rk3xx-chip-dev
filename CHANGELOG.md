# CHANGELOG

## v2.0.0 (2026-09-08) — 专业优化版

基于 6 个 Rockchip 官方库真实头文件校准，修复 API 准确性问题，补充关键缺失内容。

### 新增
- `references/official-libs.md`：6 官方库本地路径索引 + 版本锁定 + 头文件映射
- `references/rknn-model-zoo.md`：27 个现成 RKNN 模型 + 平台支持矩阵 + 使用流程
- `boards/rk3562.md`：RK3562 芯片能力描述（model zoo 支持）
- `CHANGELOG.md` + `LICENSE`（Apache 2.0）
- SKILL.md：版本锁定节、git 管理节、frontmatter version 字段

### 修复（基于官方库真实头文件校准）
- 模板 cpp-mpp/cpp-mpp-rknn-rga/cpp-zero-copy：旧 API `MPP_DEC_SET_PARSER_SPLIT_MODE` → 新 API `MPP_DEC_SET_CFG` + `mpp_dec_cfg_set_u32`（来源：`mpp/inc/rk_vdec_cfg.h`）
- 模板 cpp-mpp-rknn-rga/cpp-zero-copy：`imresize(src,dst,&srect,&drect,0)` 签名错误 → 改用 `improcess`（来源：`librga/include/im2d_single.h:502`）
- `boards/rv1106.md`：RKNN runtime 路径修正为 `armhf-uclibc/`（非 `armhf/`）

### 补充
- `registry.yaml`：新增 rk3562 板子注册
- ffmpeg-rk 完整编解码器列表（av1/vp8/vp9 解码、h264/hevc/mjpeg 编码）
- RKNN 高级 API（custom_op、matmul）索引

## v1.0.0 (2026-09-01) — 初始三性重构版

从原 `rk3xxx-dev` skill 重构，遵循通用性/适配性/专一性三原则。

### 初始内容
- 5 芯片能力描述（RK3566/3568/3576/3588/RV1106）+ registry.yaml 参数化
- 8 个 references（MPP/RKNN/RGA/VPU/DRM/DMA-BUF/ffmpeg-rk/SDK）
- 7 个 knowledge（glibc/ringbuf/Go GC/systemd/安全/对齐/调试）
- 8 个 templates（含新增 cpp-zero-copy 零拷贝管线）
- 4 个 scripts + 6 个 docker 文件，全部参数化
- AGENTS.md 多智能体适配