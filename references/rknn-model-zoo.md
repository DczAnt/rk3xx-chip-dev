# RKNN Model Zoo（NPU 推理首选起点）

> 来源：`E:\Prospace\RAG\RKProjects_lib\rknn_model_zoo`
> README：`README_CN.md` / `README.md`
>
> **NPU 开发第一步**：先从 model zoo 选现成模型跑通，再做自定义模型。
> 27 个模型覆盖检测/分类/分割/语音/NLP，含 Python + C demo + 预转换 .rknn。

## 支持平台

- 完整支持：`RK3562` `RK3566` `RK3568` `RK3576` `RK3588` `RV1126B`
- 部分支持：`RV1103` `RV1106`（armhf-uclibc）
- 旧平台：`RK1808` `RK3399PRO` `RV1109` `RV1126`

## 模型清单（27 个）

| 类别 | 模型 | 精度 | RK3568 | RK3576 | RK3588 | RV1106 |
|------|------|------|--------|--------|--------|--------|
| 图像分类 | mobilenet | FP16/INT8 | ✓ | ✓ | ✓ | ✓ |
| 图像分类 | resnet | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 物体检测 | yolov5 | FP16/INT8 | ✓ | ✓ | ✓ | ✓ |
| 物体检测 | yolov6 | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 物体检测 | yolov7 | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 物体检测 | yolov8 | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 物体检测 | yolov8_obb | INT8 | ✓ | ✓ | ✓ | ✗ |
| 物体检测 | yolov10 | FP16/INT8 | ✓ | ✓ | ✓ | ✓ |
| 物体检测 | yolo11 | FP16/INT8 | ✓ | ✓ | ✓ | ✓ |
| 物体检测 | yolox | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 物体检测 | ppyoloe | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 物体检测 | yolo_world | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 关键点 | yolov8_pose | INT8 | ✓ | ✓ | ✓ | ✗ |
| 分割 | deeplabv3 | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 分割 | yolov5_seg | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 分割 | yolov8_seg | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 分割 | ppseg | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 分割 | mobilesam | FP16 | ✓ | ✓ | ✓ | ✗ |
| 人脸 | RetinaFace | INT8 | ✓ | ✓ | ✓ | ✗ |
| 车牌 | LPRNet | FP16/INT8 | ✓ | ✓ | ✓ | ✓ |
| 文字检测 | PPOCR-Det | FP16/INT8 | ✓ | ✓ | ✓ | ✗ |
| 文字识别 | PPOCR-Rec | FP16 | ✓ | ✓ | ✓ | ✗ |
| NLP | lite_transformer | FP16 | ✓ | ✓ | ✓ | ✗ |
| 图文匹配 | clip | FP16 | ✓ | ✓ | ✓ | ✗ |
| 语音识别 | wav2vec2 | FP16 | ✓ | ✓ | ✓ | ✗ |
| 语音识别 | whisper | FP16 | ✓ | ✓ | ✓ | ✗ |
| 语音识别 | zipformer | FP16 | ✓ | ✓ | ✓ | ✗ |
| 语音分类 | yamnet | FP16 | ✓ | ✓ | ✓ | ✗ |
| TTS | mms_tts | FP16 | ✓ | ✓ | ✓ | ✗ |

## 使用流程

```
1. 选模型 → examples/<model>/
2. 模型转换 → python convert.py --target <soc>  (产 .rknn)
3. C demo 编译 → bash build-linux.sh -t <soc>
4. 部署 → scp demo + .rknn 到板子运行
```

### 编译工具链要求（来源：README_CN.md）
- aarch64：`gcc-linaro-6.3.1`
- armhf：`gcc-arm-8.3`
- armhf-uclibc（RV1106/RV1103）：`armhf-uclibcgnueabihf`

### 模型下载
- 网盘：`https://console.zbox.filez.com/l/8ufwtG`（提取码：`rknn`）
- 或从对应 GitHub 仓库导出

## 与 skill 的关系

- **新建 NPU 项目**：优先从 `examples/<model>/` 复制 C demo 改造，而非从零写
- **模型转换**：用 rknn-toolkit2（PC 端），指定 `target_platform` 匹配板子
- **多核并行**：YOLO 类高吞吐场景，参考 `references/rknn-api.md` 多核节
- **零拷贝**：model zoo 的 C demo 默认用 `rknn_inputs_set`（有拷贝），
  零拷贝需改用 `rknn_set_io_mem`，见 `references/dma-buf-zero-copy.md`