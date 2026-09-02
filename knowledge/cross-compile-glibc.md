# 交叉编译 glibc 兼容（通用技术知识库）

> **通用技术**：glibc 版本兼容是任意交叉编译场景的常见问题，非 RK 特有。
> RK 场景高频出现（容器 glibc 2.35 vs 板子 glibc 2.31），故收录为知识库。

## 一、核心问题

```
容器: Ubuntu 22.04, glibc 2.35, __libc_start_main@GLIBC_2.34
板子: Ubuntu 20.04, glibc 2.31, __libc_start_main@GLIBC_2.17

容器交叉编译的动态二进制 → 要求 GLIBC_2.34 → 板子报错:
  /tmp/demo: /lib/aarch64-linux-gnu/libc.so.6: version `GLIBC_2.34' not found
```

## 二、根因分析

1. **crt1.o 版本绑定**: 容器 `Scrt1.o` 引用 `__libc_start_main@GLIBC_2.34`
2. **glibc 2.34 变更**: `__libc_start_main` 从 libc.so.6 移到 ld-linux-aarch64.so.1
3. **--sysroot 不够**: 影响库搜索路径，但不影响 crt 文件选择
4. **符号版本**: `@GLIBC_2.34` 硬编码在目标文件中

## 三、解决方案 A：静态链接（最简单）

```bash
aarch64-linux-gnu-gcc -o demo demo.c -static          # C
go build -ldflags '-extldflags "-static"' -o demo .    # Go
cargo build --target aarch64-unknown-linux-musl        # Rust
```

- 优点: 零 glibc 依赖，任何 Linux 板子都能运行
- 缺点: 二进制较大，无法链接 RKNN/MPP 等 .so 库

## 四、解决方案 B：板子 crt + --sysroot（动态链接 SDK）

```bash
aarch64-linux-gnu-gcc -nostartfiles \
    /opt/board-crt/Scrt1.o /opt/board-crt/crti.o \
    -o demo demo.c \
    --sysroot=/opt/rk3568-board-sysroot \
    -L/opt/rk3568-sysroot/usr/lib/aarch64 \
    -L/opt/rk3568-board-sysroot/usr/lib/aarch64-linux-gnu \
    -Wl,--allow-shlib-undefined \
    -lrknnrt -lc \
    /opt/board-crt/crtn.o
```

- 优点: 可链接 RKNN/MPP .so，二进制小
- 缺点: 链接命令复杂，板子必须有对应 .so

**三要素**:
1. `-nostartfiles` — 禁用容器 crt
2. `/opt/board-crt/Scrt1.o crti.o ... crtn.o` — 板子 glibc 2.31 crt
3. `--sysroot=/opt/rk3568-board-sysroot` — 板子 glibc sysroot

## 五、何时需要板子 crt 方案

| 容器 glibc | 板子 glibc | 方案 |
|-----------|-----------|------|
| 2.35 | 2.35 (RK3576/RK3588) | 直接动态链接，**无需**板子 crt |
| 2.35 | 2.31 (RK3568) | **必须**板子 crt 方案 |
| 2.35 | 2.34 (部分 RV1106) | **必须**板子 crt 方案 |

> 判断：容器 glibc > 板子 glibc → 需要板子 crt 方案。

## 六、验证 glibc 兼容性

```bash
# 检查二进制需要的 GLIBC 版本
aarch64-linux-gnu-nm -D demo | grep GLIBC_2

# 正确 (板子可运行): 只有 GLIBC_2.17
#   U __libc_start_main@GLIBC_2.17

# 错误 (板子报错): 有 GLIBC_2.34
#   U __libc_start_main@GLIBC_2.34  ← 需改用板子 crt 方案

# 检查动态库依赖
aarch64-linux-gnu-readelf -d demo | grep NEEDED

# 检查架构
file demo  # 应: ELF 64-bit LSB executable, ARM aarch64
```

## 七、常见陷阱

| 陷阱 | 症状 | 解决 |
|------|------|------|
| 直接动态链接 | GLIBC_2.34 not found | 用板子 crt + nostartfiles |
| 替换容器 crt 后静态链接 | __libc_csu_init 未定义 | 静态链接用容器原始 crt，板子 crt 在 `/opt/board-crt/` |
| sysroot 缺 libc-2.31.so | cannot find libc.so.6 | 从板子 scp 实际 .so 文件 |
| 缺 libc_nonshared.a | __libc_csu_init 未定义 | 从板子拉取 libc_nonshared.a |
| 缺 -L${BOARD_LIB} | 找不到 -lc | 显式加 -L 板子 lib 路径 |
| RGA 静态 + RKNN 动态混合 | 链接冲突 | 统一用板子 crt 动态方案 |
| SDK tar 符号链接断裂 | cannot find -lrga | 打包用 `tar -h` 解引用符号链接 |