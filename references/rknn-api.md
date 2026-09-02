# RKNN NPU API 参考

> RKNN 是 Rockchip NPU 推理框架。本文件覆盖 C/Python API、多核并行、零拷贝、模型转换。
> 芯片 NPU 核数/能力见 `boards/<soc>.md`。

## 一、C API 核心结构体

```c
typedef uint64_t rknn_context;

typedef struct _rknn_input {
    uint32_t index;
    void* buf;                   // 输入数据
    uint32_t size;
    uint8_t pass_through;        // 0=需转换, 1=直传
    rknn_tensor_type type;       // RKNN_TENSOR_UINT8 等
    rknn_tensor_format fmt;      // RKNN_TENSOR_NHWC 等
} rknn_input;

typedef struct _rknn_output {
    uint8_t want_float;          // 是否转 float 输出
    uint32_t index;
    void* buf;
    uint32_t size;
} rknn_output;

typedef struct _rknn_tensor_attr {
    uint32_t n_dims;
    uint32_t dims[16];
    char name[256];
    uint32_t n_elems;
    uint32_t size;
    rknn_tensor_format fmt;      // NCHW/NHWC
    rknn_tensor_type type;       // FLOAT32/INT8/UINT8
    rknn_tensor_qnt_type qnt_type;
    int8_t fl;                   // DFP 小数长度
    int32_t zp;                  // 量化零点
    float scale;                 // 量化缩放因子
    uint32_t w_stride;
    uint32_t size_with_stride;
} rknn_tensor_attr;

typedef struct _rknn_tensor_mem {  // 零拷贝 API
    void* virt_addr;
    uint64_t phys_addr;
    int32_t fd;                  // DMA-BUF fd
    uint32_t size;
} rknn_tensor_mem;
```

## 二、核心 API 函数

```c
// 初始化/销毁
int rknn_init(rknn_context* ctx, void* model, uint32_t size, uint32_t flag, rknn_init_extend* extend);
int rknn_destroy(rknn_context ctx);
int rknn_dup_context(rknn_context* ctx_in, rknn_context* ctx_out);  // 权重共享

// 查询
int rknn_query(rknn_context ctx, rknn_query_cmd cmd, void* info, uint32_t size);

// 标准输入/推理/输出
int rknn_inputs_set(rknn_context ctx, uint32_t n_inputs, rknn_input inputs[]);
int rknn_run(rknn_context ctx, rknn_run_extend* extend);
int rknn_outputs_get(rknn_context ctx, uint32_t n_outputs, rknn_output outputs[], ...);
int rknn_outputs_release(rknn_context ctx, uint32_t n_outputs, rknn_output outputs[]);

// NPU 核心设置（多核芯片）
int rknn_set_core_mask(rknn_context ctx, rknn_core_mask core_mask);

// 零拷贝 API (推荐)
rknn_tensor_mem* rknn_create_mem(rknn_context ctx, uint32_t size);
rknn_tensor_mem* rknn_create_mem_from_fd(rknn_context ctx, int32_t fd, void* virt_addr, uint32_t size, int32_t offset);
int rknn_set_io_mem(rknn_context ctx, rknn_tensor_mem* mem, rknn_tensor_attr* attr);
int rknn_destroy_mem(rknn_context ctx, rknn_tensor_mem* mem);
```

### core_mask 枚举

```c
RKNN_NPU_CORE_AUTO   = 0   // 自动分配
RKNN_NPU_CORE_0      = 1   // Core 0
RKNN_NPU_CORE_1      = 2   // Core 1 (RK3576/RK3588)
RKNN_NPU_CORE_2      = 4   // Core 2 (RK3588)
RKNN_NPU_CORE_0_1    = 3   // Core 0+1
RKNN_NPU_CORE_0_1_2  = 7   // Core 0+1+2
RKNN_NPU_CORE_ALL    = 0xffff
```

---

## 三、标准 API 流程

```c
#include "rknn_api.h"

// 1. 加载模型
unsigned char* model = load_model("model.rknn", &model_len);
rknn_context ctx;
rknn_init(&ctx, model, model_len, 0, NULL);

// 2. 查询 IO
rknn_input_output_num io_num;
rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));

// 3. 设置输入 (NHWC uint8)
rknn_input inputs[1] = {0};
inputs[0].index = 0;
inputs[0].type = RKNN_TENSOR_UINT8;
inputs[0].fmt  = RKNN_TENSOR_NHWC;
inputs[0].buf  = image_data;
inputs[0].size = width * height * 3;
rknn_inputs_set(ctx, 1, inputs);

// 4. 推理
rknn_run(ctx, NULL);

// 5. 获取输出 (float32)
rknn_output outputs[io_num.n_output] = {0};
for (int i = 0; i < io_num.n_output; i++) {
    outputs[i].want_float = 1;
    outputs[i].index = i;
}
rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);

// 6. 后处理
float* data = (float*)outputs[0].buf;

// 7. 释放
rknn_outputs_release(ctx, io_num.n_output, outputs);
rknn_destroy(ctx);
```

---

## 四、零拷贝 API（推荐，性能更优）

```c
// 1. 初始化 + 查询 IO (同上)

// 2. 创建输入/输出内存 (零拷贝)
input_attrs[0].type = RKNN_TENSOR_UINT8;
input_attrs[0].fmt  = RKNN_TENSOR_NHWC;
rknn_tensor_mem* input_mem = rknn_create_mem(ctx, input_attrs[0].size_with_stride);
memcpy(input_mem->virt_addr, image_data, input_size);

rknn_tensor_mem* output_mems[io_num.n_output];
for (int i = 0; i < io_num.n_output; i++) {
    output_attrs[i].type = RKNN_TENSOR_FLOAT32;
    output_mems[i] = rknn_create_mem(ctx, output_attrs[i].n_elems * sizeof(float));
}

// 3. 设置 IO 内存
rknn_set_io_mem(ctx, input_mem, &input_attrs[0]);
for (int i = 0; i < io_num.n_output; i++)
    rknn_set_io_mem(ctx, output_mems[i], &output_attrs[i]);

// 4. 推理
rknn_run(ctx, NULL);

// 5. 直接读取输出 (无需拷贝)
float* out_data = (float*)output_mems[0]->virt_addr;

// 6. 释放
rknn_destroy_mem(ctx, input_mem);
for (int i = 0; i < io_num.n_output; i++) rknn_destroy_mem(ctx, output_mems[i]);
rknn_destroy(ctx);
```

---

## 五、NPU 多核并行（按芯片能力）

### 单核（RK3566/RK3568/RV1106）

```c
rknn_context ctx;
rknn_init(&ctx, model, len, 0, NULL);
// 无需 core_mask，默认 CORE_0
// 设置 NPU_CORE_1/CORE_0_1 无效（单核芯片不支持）
```

### 双核（RK3576）/ 三核（RK3588）

```c
// ✅ 正确: N 个独立 context + N 个推理线程
rknn_context ctx0, ctx1;  // RK3588 再加 ctx2
rknn_init(&ctx0, model, len, 0, NULL);
rknn_set_core_mask(ctx0, RKNN_NPU_CORE_0);
rknn_init(&ctx1, model, len, 0, NULL);
rknn_set_core_mask(ctx1, RKNN_NPU_CORE_1);
// 每个 context 需独立的 input_mem / output_mems
// 推理线程各处理不同通道子集, 无锁竞争

// ❌ 错误: 单 context 设 CORE_0_1 无效 (Core1 仍 0%)
// rknn_set_core_mask(ctx, RKNN_NPU_CORE_0_1);  // 不工作!
```

### 权重共享（多 context 同模型）

```c
rknn_context ctx0, ctx1;
rknn_init(&ctx0, model, len, 0, NULL);
rknn_dup_context(&ctx0, &ctx1);  // 共享权重内存，省内存
rknn_set_core_mask(ctx0, RKNN_NPU_CORE_0);
rknn_set_core_mask(ctx1, RKNN_NPU_CORE_1);
```

---

## 六、Python API (rknnlite，板端运行)

```python
from rknnlite.api import RKNNLite
import cv2, numpy as np

rknn = RKNNLite(verbose=True)
rknn.load_rknn('model.rknn')
rknn.init_runtime(target=None, core_mask=RKNNLite.NPU_CORE_AUTO)  # 板端 target=None

img = cv2.imread('test.jpg')
img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)   # 必须 BGR→RGB
img = cv2.resize(img, (640, 640))
img = np.expand_dims(img, 0)  # NHWC: [1, H, W, 3]

outputs = rknn.inference(inputs=[img])
rknn.release()
```

### core_mask (Python)

```python
RKNNLite.NPU_CORE_AUTO = 0
RKNNLite.NPU_CORE_0    = 1
RKNNLite.NPU_CORE_1    = 2       # RK3576/RK3588
RKNNLite.NPU_CORE_2    = 4       # RK3588
RKNNLite.NPU_CORE_0_1  = 3
RKNNLite.NPU_CORE_0_1_2 = 7
```

---

## 七、模型转换 (rknn-toolkit2，x86 开发机)

```python
from rknn.api import RKNN

rknn = RKNN(verbose=False)
rknn.config(mean_values=[[0,0,0]], std_values=[[255,255,255]], target_platform='rk3568')
# target_platform: 'rk3566'/'rk3568'/'rk3576'/'rk3588'/'rv1106' 等
rknn.load_onnx(model='model.onnx')  # 或 load_tensorflow, load_pytorch
rknn.build(do_quantization=True, dataset='dataset.txt')
rknn.export_rknn('model.rknn')
rknn.release()
```

### 预处理配置对照表

| 模型类型 | mean_values | std_values |
|---------|------------|-----------|
| YOLO 系列 | `[[0,0,0]]` | `[[255,255,255]]` |
| MobileNet/ResNet | `[[123.675,116.28,103.53]]` | `[[58.395,57.12,57.375]]` |
| DeepLabV3 | `[[127.5,127.5,127.5]]` | `[[127.5,127.5,127.5]]` |

### 量化类型

| 平台 | 量化类型 |
|------|---------|
| RK3566/3568/3576/3588/RV1106 | INT8 |
| RKNPU1 旧芯片 (RK1808/RV1109/RV1126) | UINT8 |

### 混合量化（对量化敏感的层保持 fp16）

```python
rknn.hybrid_quantization_step1(
    dataset='dataset.txt',
    custom_hybrid=[['layer_name_1', 'layer_name_2'], ...]
)
rknn.hybrid_quantization_step2(model_input=..., data_input=..., model_quantization_cfg=...)
```

---

## 八、预处理/后处理

### YOLO Letterbox 预处理

```python
def letterbox(image, size, bg_color=(0,0,0)):
    target_w, target_h = size
    h, w, _ = image.shape
    scale = min(target_w / w, target_h / h)
    new_w, new_h = int(w * scale), int(h * scale)
    image = cv2.resize(image, (new_w, new_h), interpolation=cv2.INTER_AREA)
    result = np.full((target_h, target_w, 3), bg_color, dtype=np.uint8)
    offset_x = (target_w - new_w) // 2
    offset_y = (target_h - new_h) // 2
    result[offset_y:offset_y+new_h, offset_x:offset_x+new_w] = image
    return result, scale, offset_x, offset_y
# YOLO 检测 pad_color=(0,0,0); YOLO 分割 pad_color=(114,114,114)
```

### DFL 解码 (YOLOv8/v10/yolo11)

```python
.5
import torch
def dfl(position):
    x = torch.tensor(position)
    n, c, h, w = x.shape
    p_num = 4; mc = c // p_num
    y = x.reshape(n, p_num, mc, h, w).softmax(2)
    acc = torch.tensor(range(mc)).float().reshape(1, 1, mc, 1, 1)
    return (y * acc).sum(2).numpy()
```

### NMS

```python
def nms_boxes(boxes, scores, NMS_THRESH=0.45):
    x, y = boxes[:, 0], boxes[:, 1]
    w, h = boxes[:, 2] - boxes[:, 0], boxes[:, 3] - boxes[:, 1]
    areas = w * h
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]; keep.append(i)
        xx1 = np.maximum(x[i], x[order[1:]]); yy1 = np.maximum(y[i], y[order[1:]])
        xx2 = np.minimum(x[i]+w[i], x[order[1:]]+w[order[1:]])
        yy2 = np.minimum(y[i]+h[i], y[order[1:]]+h[order[1:]])
        inter = np.maximum(0, xx2-xx1+1e-5) * np.maximum(0, yy2-yy1+1e-5)
        ovr = inter / (areas[i] + areas[order[1:]] - inter)
        order = order[np.where(ovr <= NMS_THRESH)[0] + 1]
    return np.array(keep)
```

### 量化输出反量化 (C)

```c
float dequant_value = (int8_value - zp) * scale;
// zp 和 scale 来自 rknn_tensor_attr
```

---

## 九、错误码

| 宏 | 值 | 说明 |
|----|-----|------|
| RKNN_SUCC | 0 | 成功 |
| RKNN_ERR_FAIL | -1 | 失败 |
| RKNN_ERR_DEVICE_UNAVAILABLE | -3 | NPU 设备不可用 |
| RKNN_ERR_PARAM_INVALID | -5 | 参数无效 |
| RKNN_ERR_MODEL_INVALID | -6 | 模型无效 |
| RKNN_ERR_CTX_INVALID | -7 | 上下文无效 |
| RKNN_ERR_DEVICE_UNMATCH | -10 | SDK/驱动版本不匹配 |

---

## 十、关键注意事项

1. **输入格式 NHWC uint8**: RKNN 内部自动归一化，不需手动 /255
2. **BGR2RGB**: OpenCV 读取为 BGR，推理前必须转 RGB
3. **Letterbox 填充色**: YOLO 检测用 `(0,0,0)`，YOLO 分割用 `(114,114,114)`
4. **单核芯片 core_mask**: RK3566/3568/RV1106 仅 `NPU_CORE_AUTO`/`NPU_CORE_0` 有效
5. **多核必须多 context**: 单 context 设 `CORE_0_1`/`CORE_ALL` 无效
6. **零拷贝 API 更优**: 优先 `rknn_set_io_mem` 而非 `rknn_inputs_set`
7. **量化输出反量化**: `(int8_value - zp) * scale`
8. **RV1106 库名**: `librknnmrt.so`（mrt 后缀），链接 `-lrknnmrt`
9. **模型平台绑定**: `target_platform` 不可跨平台
10. **NPU 频率调节**: `echo performance > /sys/class/devfreq/<npu>/governor`