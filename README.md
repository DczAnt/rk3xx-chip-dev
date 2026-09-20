# rk3xx-chip-dev — Rockchip RK3XX 芯片开发 AI 技能包

> 🔗 **仓库地址：https://github.com/DczAnt/rk3xx-chip-dev**
>
> 把 Rockchip 芯片开发中踩过的 **56 个陷阱 + 46 条准则**做成 AI 编程智能体可直接调用的技能包：
> AI 写代码、交叉编译、部署上板、调试排障时**自动避开这些坑**。
> 坑踩一次，写进技能包，之后所有项目都不再踩。

[![License](https://img.shields.io/badge/license-Apache%202.0-green.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-2.0.1-blue.svg)](CHANGELOG.md)
[![Platform](https://img.shields.io/badge/SoC-RK3562%20%7C%20RK3566%20%7C%20RK3568%20%7C%20RK3576%20%7C%20RK3588%20%7C%20RV1106-orange.svg)](#芯片能力矩阵)

---

## 这是什么

给 AI 编程智能体（华为云 CodeArts / Cursor / Claude Code / Copilot 等）挂载的**个人级开发技能**：

- **踩坑经验资产化**：MPP 硬编解码、RKNN NPU 推理、RGA 2D 加速、DMA-BUF 零拷贝、DRM 显示、交叉编译 glibc 兼容——每个领域的高频坑都以"现象 → 根因 → 解法"三元组沉淀
- **AI 按工作流执行**：读板子能力 → 选编译策略 → 生成代码 → 部署调试，每步先查陷阱清单再动手
- **参数化不绑板**：不硬编码任何 IP/密码/路径，全部走 `boards/registry.yaml` 或 CLI 参数，clone 下来填上你的板子信息即可用

**实测背书**：基于本技能开发的 [RK3576 八路 AI 视频监控系统](https://github.com/DczAnt/ai_video_monitor_rk3576)，8 路并发推理 84.27 fps，NPU 双核利用率 61%/60%，全链路 DMA-BUF 零拷贝。

## 快速开始

```bash
# 1. 克隆到智能体技能目录（以 CodeArts 为例）
git clone https://github.com/DczAnt/rk3xx-chip-dev.git ~/.codeartsdoer/skills/rk3xx-chip-dev

# 2. 填写你的板子信息（IP/凭据，本文件不入库）
vi boards/registry.yaml

# 3. 对话中提到 RK 芯片相关关键词即自动激活
#    例："帮我在 RK3576 上跑 YOLOv8 推理，RTSP 拉流硬解码"
```

## 仓库导览

| 目录 | 内容 |
|------|------|
| `SKILL.md` | 技能入口：触发词、工作流、芯片能力速查、链接策略决策树 |
| `boards/` | 6 款 SoC 能力描述 + `registry.yaml` 板子参数化配置 + 自动探测脚本 |
| `references/` | RK 专有知识：MPP / RKNN / RGA / VPU / DRM / DMA-BUF / ffmpeg-rockchip / model zoo |
| `knowledge/` | 通用工程知识：glibc 兼容 / 环形缓冲 IPC / Go 内存管理 / systemd / 安全加固 / 结构体对齐 |
| `templates/` | 8+ 项目脚手架：C / C++ / Go / Rust / CMake，含零拷贝管线模板，可直接复制 |
| `scripts/` | 板子探测 / 部署 / 构建 / 诊断脚本，全部参数化 |
| `docker/` | 交叉编译镜像构建文件（可选，板上有 gcc 时可原生编译） |
| `AGENTS.md` | 多智能体适配声明（CodeArts / Cursor / Claude Code / Aider / Continue / Cline） |

## 芯片能力矩阵

| 能力 | RK3566/3568 | RK3576 | RK3588 | RV1106 |
|------|:---:|:---:|:---:|:---:|
| NPU 多核并行 | ✗ 单核 | ✓ 双核 2 context | ✓ 三核 3 context | ✗ |
| RGA 多实例均衡 | ✗ | ✓ 双实例 | ✓ 三实例 | ✗ |
| VPU JPEG 硬编 | ✗ | ✓ mjpeg_rkmpp | ✓ | ✓ |
| big.LITTLE 绑核 | ✗ 同构 | ✓ A53+A72 | ✓ A55+A76 | ✗ |

完整矩阵与 API 差异见 `boards/*.md`。

## 如何自制你自己的 AI 技能包（skill）

**思想一句话**：AI 智能体每次会话都是"失忆"的，skill 就是把你的踩坑经验变成它每次都能读到的"长期记忆"。

以下五步法即本仓库的实践路径，照做即可：

### 第 1 步：积累原始踩坑记录

开发过程中随手记录：报错信息、排查路径（哪些方向排除了）、最终根因、修复代码。不用整理格式，先记下来。**素材越多，第 2 步越容易提炼。**

### 第 2 步：提炼"陷阱三元组"

把原始记录收敛成统一格式，AI 才能稳定消费：

```
陷阱 N：一句话标题（如 "NPU 双核假激活"）
- 现象：可观察到的错误表现（Core1 利用率永远 0%）
- 根因：技术原因（单 context 设 CORE_0_1 无效，驱动只认独立 context）
- 解法：可直接执行的修复（创建 2 个独立 RKNN context 分别 rknn_set_core_mask）
```

每条陷阱必须**二元可判定**——AI 能据此明确判断"当前情况是否命中此陷阱"。

### 第 3 步：写 SKILL.md 入口文件

skill 的核心是一个 `SKILL.md`，关键结构：

```markdown
---
name: your-skill-name
description: 一段包含【触发词】的描述——AI 靠它判断何时加载本技能
allowed-tools: Read, Write, Edit, Bash, Glob, Grep
---

# 工作流（AI 的执行路径：先做什么后做什么）
# 陷阱清单（第 2 步的三元组，AI 动手前必查）
# 关键代码模板（可直接复制的正确写法）
```

**description 写法决定激活率**：把用户可能说出的关键词全部列进去（本仓库 description 列了 MPP/RKNN/RGA/交叉编译/零拷贝等 20+ 触发词）。

### 第 4 步：参数化脱敏

如果 skill 要开源或跨项目复用，**必须**把个人环境信息抽离：

| 个人环境信息 | 抽离方式 |
|--------------|----------|
| 板子 IP / SSH 凭据 | 配置文件（如 `registry.yaml`，gitignore）或 CLI 参数 |
| 摄像头 / 设备地址 | 同上 |
| 本机绝对路径 | 相对路径或环境变量 |

检查命令：`grep -rn "密码\|password\|192.168\." SKILL.md references/`，逐条确认是占位符或通用示例。

### 第 5 步：挂载验证 + 持续沉淀

1. 放入智能体技能目录（CodeArts: `~/.codeartsdoer/skills/<name>/`），新会话提及触发词验证激活
2. **沉淀循环**：之后每次踩新坑 → 记录 → 周期性回写 skill 的陷阱清单 → 版本号 +1（见本仓库 [CHANGELOG.md](CHANGELOG.md) 的演进记录）

> 技能包的价值随时间**复利增长**：第 1 个月它避开 10 个坑，半年后它避开 56 个——这就是"坑踩一次，永不再踩"。

## 多智能体适配

本技能 agent-agnostic：`SKILL.md` 是唯一入口，其余全是普通 Markdown/代码。各智能体接入方式见 [AGENTS.md](AGENTS.md)（Cursor/Claude Code 用 symlink，Aider/Continue 用配置引用）。

## 相关项目

- [ai_video_monitor_rk3576](https://github.com/DczAnt/ai_video_monitor_rk3576) — 基于本技能开发的八路 AI 视频监控系统（实战验证）

## License

[Apache 2.0](LICENSE)