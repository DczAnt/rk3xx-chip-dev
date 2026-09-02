# SDK sysroot 与交叉编译链接策略

> SDK 头文件/库路径因芯片而异，本文件提供通用链接策略决策树。
> 具体路径见 `boards/<soc>.md`，glibc 兼容细节见 `knowledge/cross-compile-glibc.md`。

## 一、SDK 库清单

| 库 | 类型 | 用途 | 链接方式 |
|----|------|------|---------|
| librockchip_mpp.so | 动态 (仅 .so) | MPP 硬件编解码 | 必须动态 |
| librknnrt.so | 动态 (仅 .so) | RKNN NPU 推理 | 必须动态 |
| librknnmrt.so | 动态 (仅 .so) | RV1106 NPU 推理 | 必须动态 |
| librga.so | 动态 | RGA 2D 加速 | 动态或静态 |
| librga.a | 静态 | RGA 2D 加速 | 可静态链接 |

> **关键**: librknnrt / librockchip_mpp 只有 .so，必须动态链接；librga 有 .a 可静态链接。

---

## 二、链接策略决策树

```
项目是否需要 SDK 库 (MPP / RKNN / RGA)?
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
            ├── 一致 (如 RK3576: 容器 2.35 = 板子 2.35)
            │   └── 直接动态链接
            └── 不一致 (如 RK3568: 容器 2.35 > 板子 2.31)
                └── 板子 crt 方案 (三要素):
                    1. -nostartfiles              # 禁用容器 crt
                    2. /opt/board-crt/Scrt1.o crti.o ... crtn.o  # 板子 glibc crt
                    3. --sysroot=/opt/rk3568-board-sysroot       # 板子 glibc sysroot
                    详见 knowledge/cross-compile-glibc.md
```

---

## 三、动态链接完整模板（C + RKNN + MPP + RGA）

```bash
# 环境变量（路径按 boards/<soc>.md 调整）
SDK_INC=/opt/rk3568-sysroot/usr/include
SDK_LIB=/opt/rk3568-sysroot/usr/lib/aarch64
BOARD_SYSROOT=/opt/rk3568-board-sysroot
BOARD_LIB=${BOARD_SYSROOT}/usr/lib/aarch64-linux-gnu
BOARD_CRT=/opt/board-crt

aarch64-linux-gnu-g++ -nostartfiles \
    ${BOARD_CRT}/Scrt1.o ${BOARD_CRT}/crti.o \
    -o demo demo.cpp \
    --sysroot=${BOARD_SYSROOT} \
    -I${SDK_INC} -I${SDK_INC}/rockchip \
    -L${SDK_LIB} -L${BOARD_LIB} \
    -Wl,--allow-shlib-undefined \
    -lrockchip_mpp -lrknnrt -lrga -lc \
    ${BOARD_CRT}/crtn.o
```

---

## 四、CMake 工具链

```cmake
# /opt/cross-toolchain/aarch64-linux-gnu.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR aarch64-linux-gnu-ar)
set(CMAKE_RANLIB aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP aarch64-linux-gnu-strip)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(rk_app C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

set(RK_SYSROOT "/opt/rk3568-sysroot" CACHE PATH "RK SDK sysroot")
include_directories(${RK_SYSROOT}/usr/include ${RK_SYSROOT}/usr/include/rockchip)
link_directories(${RK_SYSROOT}/usr/lib/aarch64)

add_executable(demo main.cpp)
target_link_libraries(demo rockchip_mpp rknnrt rga)
```

### CMake 编译命令（动态链接 + 板子 crt）

```bash
mkdir -p build && cd build
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=/opt/cross-toolchain/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-nostartfiles /opt/board-crt/Scrt1.o /opt/board-crt/crti.o --sysroot=/opt/rk3568-board-sysroot -L/opt/rk3568-sysroot/usr/lib/aarch64 -L/opt/rk3568-board-sysroot/usr/lib/aarch64-linux-gnu -Wl,--allow-shlib-undefined -lc /opt/board-crt/crtn.o" \
    -DCMAKE_FIND_ROOT_PATH=/opt/rk3568-board-sysroot
make -j$(nproc)
```

---

## 五、Go 交叉编译

```bash
# 纯 Go 静态链接
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -o demo

# Go + CGO 静态链接 (调用 C 库, 推荐)
CGO_ENABLED=1 GOOS=linux GOARCH=arm64 \
    CC=aarch64-linux-gnu-gcc \
    CGO_LDFLAGS="-static" \
    go build -ldflags '-extldflags "-static"' -o demo .
```

---

## 六、Rust 交叉编译

```bash
# Rust musl 静态链接 (推荐, 无 glibc 依赖)
cargo build --release --target aarch64-unknown-linux-musl
# 输出: target/aarch64-unknown-linux-musl/release/demo

# Rust glibc 动态链接 (需板子 crt 方案)
cargo build --release --target aarch64-unknown-linux-gnu
```

### Cargo 交叉链接配置

```toml
# /root/.cargo/config.toml
[target.aarch64-unknown-linux-gnu]
linker = "aarch64-linux-gnu-gcc"
rustflags = ["-C", "link-arg=-lgcc_s"]

[target.aarch64-unknown-linux-musl]
linker = "aarch64-linux-gnu-gcc"

[build]
target = "aarch64-unknown-linux-gnu"
```

---

## 七、链接策略速查

| 场景 | 推荐策略 | 理由 |
|------|---------|------|
| 纯 C/Go/Rust, 无 SDK 依赖 | 静态链接 | 最简单, 零 glibc 依赖 |
| 需要 librknnrt.so | 动态 + 板子 crt (glibc 不一致时) | RKNN 必须动态 |
| 需要 librockchip_mpp.so | 动态 + 板子 crt (glibc 不一致时) | MPP 必须动态 |
| 仅需要 librga.a | 静态链接 | RGA 有静态库 |
| CMake 项目 + SDK | 动态 + 板子 crt | 见 CMake 模板 |
| Go + CGO + SDK | 静态 + CGO_LDFLAGS | 需测试 |
| Rust + SDK | musl + FFI | 需测试 |

---

## 八、编译后验证

```bash
# 1. 验证架构
file demo
# 应: ELF 64-bit LSB executable, ARM aarch64 (或 armhf for RV1106)

# 2. 验证 glibc 兼容性 (动态链接二进制)
aarch64-linux-gnu-nm -D demo | grep GLIBC_2
# 应只有 GLIBC_2.17 (板子 glibc 2.31 支持), 不应有 GLIBC_2.34

# 3. 检查动态库依赖
aarch64-linux-gnu-readelf -d demo | grep NEEDED

# 4. 部署并运行
scp demo root@<BOARD_IP>:/tmp/
ssh root@<BOARD_IP> "/tmp/demo"
```

---

## 九、SDK 提取（从板子打包到容器）

```bash
# 用 -h 解引用符号链接, 否则 .so → .so.1 → .so.0 链断裂
ssh root@<BOARD_IP> "cd / && tar czf /tmp/rk-sdk.tar.gz -h \
  usr/include/rockchip usr/include/rga usr/include/rknn \
  usr/lib/librknnrt.so \
  usr/lib/aarch64-linux-gnu/librockchip_mpp.so \
  usr/lib/aarch64-linux-gnu/librga.so"
# 路径按 boards/<soc>.md 调整
```

> **关键**: `tar -h` 解引用符号链接，否则 Docker 内链接报 `cannot find -lrga`。