# ESP-NN 移植到 openvela 复刻手册

## 1. 目标

本文是一份可在另一套 openvela 工作区重复执行的实操手册。完成后，ESP32-S3 的 TFLite
Micro（TFLM）能够在不改变应用层算子注册方式的前提下，为受控的 int8 `Conv2D` 和
`DepthwiseConv2D` 调用 ESP-NN。

目标不是“一次性替换所有卷积”，而是建立一条可回退的路径：

```text
应用 AddConv2D()/AddDepthwiseConv2D()
  -> TFLM ESP-NN wrapper
      -> 条件不满足：TFLM reference
      -> 条件满足且在白名单：ESP-NN
```

本手册适用于 ESP32-S3、NuttX/openvela、全 int8 模型。实现前请阅读
[实施指南](../优化文档/ESP-NN移植到openvela实施指南.md)，其中保留了架构背景、许可证和性能记录。

## 2. 完成定义

完成移植至少满足以下条件：

1. `CONFIG_TFLITEMICRO_ESP_NN=n` 时，行为与原有 TFLM reference kernel 一致；
2. `CONFIG_TFLITEMICRO_ESP_NN=y` 时，构建能找到 ESP-NN 的 C 与 Xtensa 汇编源码；
3. 不支持的算子、类型、形状、对齐条件自动回退 reference；
4. 每个待加速节点先进行逐字节 reference 对照；
5. 性能测试关闭 VERIFY 与 TRACE；
6. 生产应用只在单节点正确性、组合稳定性和性能测试都通过后启用。

## 3. 目录与仓库边界

当前推荐的验证期布局如下：

```text
openvela/
├── apps/                                  # openvela 通用应用仓
│   └── mlearning/
│       ├── tflite-micro/
│       └── esp-nn/
│           └── esp-nn -> ../../../ccf_audioevent/third_party/esp-nn
└── ccf_audioevent/                        # 项目/作品仓
    ├── third_party/
    │   └── esp-nn/                        # 跟踪的锁定上游快照
    ├── scripts/link_esp_nn.sh
    └── board/esp32s3-devkit/configs/
```

约束：

- `third_party/esp-nn` 是作品仓跟踪的上游快照；保留 `LICENSE`、上游 URL、commit SHA 和导入日期；
- `apps/mlearning/esp-nn/esp-nn` 是本机软链接，**不得提交到 apps 仓**；
- 不引入 ESP-IDF、FreeRTOS 或 `idf.py`；ESP-NN 仅作为 C/汇编源码库被编译；
- 不在 `third_party/esp-nn` 直接混入 openvela 适配逻辑，适配代码放在 TFLM wrapper。

## 4. 第一步：导入上游 ESP-NN

将锁定版本的 ESP-NN 导入为：

```text
ccf_audioevent/third_party/esp-nn/
```

最低需要保留：

```text
LICENSE
include/
src/common/
src/convolution/
```

同时创建 `third_party/esp-nn/UPSTREAM.md`，记录：

```text
upstream URL
exact commit SHA
import date
local patch list (initially: none)
license: Apache-2.0
```

并在项目的 `THIRD_PARTY_NOTICES.md` 中加入版权与许可证声明。首次移植不要修改上游
`src/convolution/*.c` 或 `.S`；如果以后必须修复上游缺陷，应以独立、可回放的补丁维护。

## 5. 第二步：建立安全的本地软链接

在 openvela 根目录执行：

```sh
./ccf_audioevent/scripts/link_esp_nn.sh
```

脚本应创建或确认：

```text
apps/mlearning/esp-nn/esp-nn
  -> ../../../ccf_audioevent/third_party/esp-nn
```

脚本必须具备以下保护：

1. 验证源目录包含 `LICENSE`、`include/`、`src/`；
2. 仅在链接缺失时创建；
3. 遇到普通目录、文件或指向其他位置的链接时失败，不覆盖；
4. 链接正确时幂等退出。

不要把 `ln -sf` 写入脚本；它可能覆盖用户已有的目录或链接。

## 6. 第三步：新增 Kconfig 开关

修改 [apps/mlearning/tflite-micro/Kconfig](../../../apps/mlearning/tflite-micro/Kconfig)。最小
backend 开关应满足：

```kconfig
config TFLITEMICRO_ESP_NN
    bool "Enable ESP-NN optimized kernels"
    default n
    depends on ARCH_CHIP_ESP32S3
    depends on !MLEARNING_CMSIS_NN && !XTENSA_HIFI
```

再新增以下 bring-up 配置：

| 配置 | 用途 | 默认值 |
| --- | --- | --- |
| `TFLITEMICRO_ESP_NN_TRACE` | 打印候选条件、tensor ID、对齐信息 | `n` |
| `TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR` | 单个 Conv2D output ID 白名单 | `-1` |
| `TFLITEMICRO_ESP_NN_CONV2D_VERIFY` | Conv2D 与 reference 逐字节比较 | `n` |
| `TFLITEMICRO_ESP_NN_DEPTHWISE_CONV2D_OUTPUT_TENSOR` | 单个 Depthwise output ID 白名单 | `-1` |
| `TFLITEMICRO_ESP_NN_DEPTHWISE_CONV2D_VERIFY` | Depthwise 与 reference 逐字节比较 | `n` |

组合性能测试可额外提供 `TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR_MASK`。bit N 选择
output tensor ID N；0 表示不额外选择节点。不要把 `VERIFY` 默认设为 `y`。

## 7. 第四步：接入 CMake 和 Make

同时修改 [CMakeLists.txt](../../../apps/mlearning/tflite-micro/CMakeLists.txt) 与
[Makefile](../../../apps/mlearning/tflite-micro/Makefile)，保证两套构建路径等价。

### 7.1 公共约束

在 `CONFIG_TFLITEMICRO_ESP_NN` 条件下：

1. 设置 `ESP_NN_DIR=apps/mlearning/esp-nn/esp-nn`；
2. `include/esp_nn.h` 不存在时给出“先运行 link_esp_nn.sh”的明确错误；
3. 添加 `ESP_NN=1`、`CONFIG_NN_OPTIMIZED=1`、`CONFIG_IDF_TARGET_ESP32S3=1`；
4. 添加 `${ESP_NN_DIR}/include` 与 `${ESP_NN_DIR}/src/common`；
5. 编译 ESP-NN 必需的 ANSI C、ESP32-S3 C 与 ESP32-S3 `.S` 文件；
6. 从 TFLM 源列表移除原始 `conv.cc`、`depthwise_conv.cc`；
7. 改为加入本项目的两个 wrapper：
   `kernels/esp_nn/conv.cc`、`kernels/esp_nn/depthwise_conv.cc`。

这一步必须做到“恰好一套注册实现”。若原始 kernel 与 wrapper 同时编译，最终会出现
`Register_CONV_2D` 或 `Register_DEPTHWISE_CONV_2D` 重复定义。

### 7.2 编译选项

ESP-NN 的 ESP32-S3 C/汇编源使用：

```text
-O2 -fno-unroll-loops -mlongcalls
```

`-mlongcalls` 仅能作用于 Xtensa 目标源，不能注入全局宿主工具编译选项；否则配置阶段生成
host 工具时会出现：

```text
gcc: error: unrecognized command-line option '-mlongcalls'
```

CMake 可通过 `set_source_files_properties()` 限定 ESP-NN 源文件；Make 路径依赖 ESP32-S3
交叉工具链上下文，不能让该选项泄漏到 NuttX 的 host 工具。

## 8. 第五步：实现两个 TFLM wrapper

wrapper 放在：

```text
apps/mlearning/tflite-micro/kernels/esp_nn/
├── conv.cc
└── depthwise_conv.cc
```

实现方式是从当前 TFLM 对应 kernel 保留 reference 分支，再在 int8 分支添加受控 dispatch；
不改写下载的 `tflite-micro/tensorflow/...` 上游目录。

### 8.1 Prepare 阶段必做事项

每个节点都应：

1. 保存 output tensor ID；
2. 计算并保存 TFLM 原有的 `OpDataConv`、padding、per-channel multiplier、shift；
3. 检查 type、NHWC 维度、batch、stride、padding、dilation、depth multiplier；
4. 检查 output ID 是否在白名单；
5. 仅对白名单节点调用 `esp_nn_get_*_scratch_size()`；
6. 用 `RequestScratchBufferInArena()` 申请 ESP-NN scratch；
7. VERIFY 打开时，再申请一块与 output 同大小的 reference output scratch。

不得在 `Eval()` 中 `malloc()`。scratch 必须来自 Tensor Arena，才能由 TFLM 管理生命周期。

### 8.2 Eval 阶段必做事项

运行时再次检查 input、output、scratch 的对齐。例如当前 S3 路径要求：

```text
input / output / scratch: 16-byte aligned
Conv2D filter:             8-byte aligned
```

Conv2D 常量 filter 若不满足 8-byte 对齐，不要直接传给 SIMD kernel；为被选中的节点分配
persistent storage，手工向上对齐后复制 filter。Depthwise 实现会将 filter 复制到自身 scratch，
但仍应针对实际内核要求检查。

执行顺序如下：

```text
不满足条件
  -> TFLM reference

满足条件且 VERIFY=y
  -> reference 写入 verify scratch
  -> ESP-NN 写入真实 output
  -> 逐字节比较
  -> 不匹配：复制 reference 到真实 output，再打印首个差异

满足条件且 VERIFY=n
  -> ESP-NN 直接写入真实 output
```

传给 ESP-NN 的 offset、padding、activation min/max、per-channel multiplier 与 shift 必须完全
来自 TFLM 的 `OpDataConv`。不要为了适配 kernel 改变量化参数或 padding。

## 9. 第六步：先建立 reference 基线

先使用不启用 ESP-NN 的 profile：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark -j8
```

烧录后记录：

```sh
tflm_benchmark --warmup 10 --repeat 50
```

记录模型 SHA、固件 SHA、Arena 实际使用量、每个 Conv/DW 平均耗时及原始串口日志。`--csv` 是
布尔开关：要输出 CSV 时使用 `--csv`，不要写成 `--csv 0`。

## 10. 第七步：单节点正确性验证

创建独立 profile，例如：

```text
ccf_audioevent/board/esp32s3-devkit/configs/
└── tflm_benchmark_espnn_verify/defconfig
```

基本配置：

```text
CONFIG_TFLITEMICRO=y
CONFIG_TFLITEMICRO_ESP_NN=y
CONFIG_TFLITEMICRO_ESP_NN_TRACE=y
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR=<目标 output ID>
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_VERIFY=y
CONFIG_XTENSA_CP_INITSET=0x0009
CONFIG_XTENSA_TOOLCHAIN_ESP=y
```

`CONFIG_XTENSA_CP_INITSET=0x0009` 用于启用当前验证所需的协处理器状态；缺失时，ESP-NN
汇编路径可能出现 CP 异常。实际值必须以当前 NuttX/ESP32-S3 port 定义为准。

先跑一次：

```sh
tflm_benchmark --warmup 0 --repeat 1 --csv
```

接受标准：

```text
candidate=1 selected=1
backend=esp-nn reason=ok
[espnn-verify] ... match bytes=<output size>
```

然后运行：

```sh
tflm_benchmark --warmup 2 --repeat 10
```

仅当十次均匹配、无卡死、无重启、无 `EXCCAUSE` 后，才切换到下一个节点。不要同时验证两个
算子，否则无法定位是哪个节点造成数值误差或异常。

## 11. 第八步：组合性能测试

每个节点通过单节点验证后，创建独立性能 profile，例如：

```text
tflm_benchmark_espnn_profile
```

性能 profile 的规则：

```text
VERIFY=n
TRACE=n
先启用少量已验证节点
每一阶段连续运行 50 次
```

对于 output ID 为 23、25、27 的三个 Conv2D：

```text
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR=-1
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR_MASK=0x0a800000
```

其中：

```text
0x0a800000 = (1 << 23) | (1 << 25) | (1 << 27)
```

运行：

```sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_profile -j8
tflm_benchmark --warmup 10 --repeat 50
```

先组合普通 Conv2D，再依次加入已验证的 Depthwise 节点。每增加一个阶段，都记录：Arena 使用量、
原始日志、是否稳定、各算子耗时和整次 Invoke 耗时。不要用开启 TRACE 的结果评价性能，因为
串口输出会污染 tick。

## 12. 常见故障与处理

| 现象 | 常见原因 | 处理 |
| --- | --- | --- |
| `ESP-NN source is unavailable` | 本地链接缺失 | 运行 `link_esp_nn.sh`，检查目标目录 |
| `undefined reference to esp_nn_*` | ESP-NN C/汇编源未进入同一静态库 | 检查 CMake/Make 源列表和链接顺序 |
| `Register_CONV_2D` 重复定义 | 原始 TFLM kernel 与 wrapper 同时编译 | 从源列表排除原始 conv/dw |
| `-mlongcalls` 被宿主 gcc 拒绝 | Xtensa 选项泄漏到 host 工具 | 仅作用于目标源/交叉工具链 |
| `AllocateTensors` 失败 | scratch + VERIFY 超出 Arena | 记录实际需求，先增大验证 profile Arena |
| `candidate=1` 但 `backend=reference` | 运行时地址未对齐 | 检查 input/output/scratch/filter 的对齐日志 |
| `mismatch` | ESP-NN 路径语义或量化参数不一致 | 保持 reference 输出，停止扩大白名单 |
| 卡死或 `EXCCAUSE` | 协处理器、汇编路径或 scratch 问题 | 确认 CP 初始化、单节点复现、检查对齐与 scratch |
| `Unknown or incomplete option: 0` | 使用了 `--csv 0` | 不输出 CSV 就省略参数；输出 CSV 使用 `--csv` |

## 13. 提交顺序与生产准入

建议每个提交只表达一个可回退阶段：

1. 导入 ESP-NN 源码、许可证、建链脚本；
2. Kconfig 与 CMake/Make 接入，但默认关闭；
3. Conv2D wrapper 与单节点 VERIFY；
4. Depthwise wrapper 与单节点 VERIFY；
5. 每个模型节点的独立验证 profile；
6. 无 VERIFY/TRACE 的组合性能 profile；
7. 文档、原始 benchmark 结果和生产开关。

生产 `audio_event` 配置只有在以下条件全部满足时才可启用：每个节点 bit-exact、组合运行稳定、
Arena 有安全余量、真实音频回归通过、性能收益可重复。否则保留 reference backend 或只启用
已证明稳定的节点。
