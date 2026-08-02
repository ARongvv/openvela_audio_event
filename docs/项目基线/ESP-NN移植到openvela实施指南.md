# ESP-NN 移植到 openvela 实施指南

## 1. 目的与范围

本文说明如何把 Espressif 的 [ESP-NN](https://github.com/espressif/esp-nn)
作为 **ESP32-S3 专用 TFLite Micro（TFLM）backend** 接入 openvela，并用于
`ccf_audioevent` 的 int8 音频事件模型。

目标是替换 TFLM 对 `Conv2D` 和 `DepthwiseConv2D` 的通用 reference integer
实现；应用层仍通过 `MicroMutableOpResolver::AddConv2D()` 和
`AddDepthwiseConv2D()` 注册算子，无须把 ESP-NN API 暴露给 `audio_event`。

首个版本只覆盖本模型实际使用的全 int8 路径：

```text
int8 input + int8 filter + int32 bias + per-channel quantization
```

`Mean`、`FullyConnected`、`Softmax` 仅在算子剖析证明其占比显著时再接入。对于
当前四分类模型，优先优化 Conv/DepthwiseConv 的风险更低、潜在收益更高。

当前工作区已完成首个实现版本：构建时以 ESP-NN wrapper 替换 Conv2D 和
DepthwiseConv2D 的注册实现，但默认 benchmark 和生产 `audio_event` 均保持 reference
backend。独立的 `tflm_benchmark_espnn_verify` profile 只对白名单中的一个 Conv2D 启用
ESP-NN 与逐字节校验；audio_event 的生产 defconfig 在数值回归与真机性能验证完成前不切换。

开始比较前应先按 [tflm_benchmark 算子剖析](../使用与调试/tflm_benchmark算子剖析.md)
取得当前模型的算子级基线。

## 2. 适用性与边界

ESP-NN 为 ESP32-S3 提供使用 LX7 向量指令的优化实现；它不是 CMSIS-NN，也不是
Xtensa HiFi backend。因此：

- 仅为 ESP32-S3 打开本 backend；
- 不启用 `CONFIG_MLEARNING_CMSIS_NN` 或 `CONFIG_XTENSA_HIFI`；
- 不引入 ESP-IDF 的运行时、FreeRTOS 或 `idf.py` 构建系统；
- Tensor Arena 继续放在内部 SRAM，不因接入 ESP-NN 移到 PSRAM；
- 不开启 ESP-NN 的 `NN_SKIP_NUDGE` 非 bit-exact 快路径。

后者可能在半移位舍入边界产生 `±1 LSB` 差异。首个可合入版本必须优先保证与
reference backend 的数值一致性。

当前 `M001-small-int8` 的算子、Arena 及性能基线见
[ESP32-S3 推理时间优化方案](ESP32-S3推理时间优化方案.md)；端侧效果和性能记录格式见
[audio_event 模型真机基准测试汇总](audio_event模型真机基准测试汇总.md)。

## 3. 目标架构

```text
audio_event
  |
  +-- event_classifier.cc
  |     `-- MicroMutableOpResolver::AddConv2D/AddDepthwiseConv2D
  |
  `-- tflite_micro 静态库
        +-- 通用 TFLM kernel：Shape、Mean、Softmax 等
        +-- openvela ESP-NN wrapper：Conv2D、DepthwiseConv2D
        `-- ESP-NN C / Xtensa 汇编源码
              +-- esp_nn_conv_s8()
              `-- esp_nn_depthwise_conv_s8()
```

wrapper 必须导出与通用 kernel 相同的 `Register_CONV_2D()` 和
`Register_DEPTHWISE_CONV_2D()`。构建系统只能编入其中一套实现；否则会出现重复
注册符号，或意外仍走 reference kernel。

## 4. 两阶段源码管理与许可证

### 4.1 当前阶段：作品仓持有源码，本地软链接接入

在本项目的验证阶段，ESP-NN 源码由 `ccf_audioevent` 持有；`apps/mlearning` 中只建立
**未跟踪的本地软链接**，让 openvela 构建系统按常规路径找到源码。这样可使作品仓包含
完整第三方依赖，同时避免过早将未验证 backend 提交到 openvela 公共仓。

推荐布局：

```text
ccf_audioevent/
└── third_party/
    └── esp-nn/                    # 作品仓跟踪的固定版本源码
        ├── LICENSE
        ├── include/
        └── src/
            ├── common/
            ├── convolution/
            ├── fully_connected/
            └── ...

apps/mlearning/
└── esp-nn/
    └── esp-nn -> ../../../ccf_audioevent/third_party/esp-nn
                                      # 本机链接，不提交到 apps 仓
```

优先从 openvela 根目录运行建链脚本；它只会创建缺失链接，遇到普通目录、文件或非预期
链接会停止，避免覆盖用户数据：

```sh
./ccf_audioevent/scripts/link_esp_nn.sh
```

也可以手动创建。创建前先确认目标路径不存在或是预期的旧链接：

```sh
mkdir -p apps/mlearning/esp-nn
ln -s ../../../ccf_audioevent/third_party/esp-nn \
  apps/mlearning/esp-nn/esp-nn
```

链接的相对路径以 openvela 根目录为基准；在其他工作区克隆时必须重新创建。构建逻辑也
应在链接缺失时明确报告，不能让 CMake 因找不到文件而报出不易定位的后续错误。

作品仓中的导入记录必须包含：

- 上游仓库 URL；
- 精确 commit SHA；
- 导入日期与导入脚本/补丁；
- Apache-2.0 LICENSE 和第三方版权声明；
- 与当前 TFLM commit 的兼容性验证结果。

不要复制 ESP-IDF 工程、测试 App 或 `idf_component.yml` 到生产构建。ESP-NN 的上游
`CMakeLists.txt` 可作为“ESP32-S3 应编译哪些 `.c/.S` 文件”的白名单参考，但不能直接
被 openvela 的 CMake 调用。

### 4.2 软链接的边界与后续迁移

该软链接只适用于 `ccf_audioevent` 的本地开发、竞赛交付和真机验证，不能作为公共
`apps` 仓的已提交依赖，原因是：

- 常规 openvela clone、CI 和上游 PR 环境不包含 `ccf_audioevent`；
- 软链接目标未就绪时，公共 TFLM backend 将不可构建；
- `apps/mlearning` 与 `ccf_audioevent` 是独立 Git 仓，链接本身不携带源代码版本。

当数值回归和真机 benchmark 证明 backend 对多个 openvela 模型均有价值时，再执行第二
阶段迁移：将已锁定并验证的 ESP-NN 源码正式移入 `apps/mlearning/esp-nn/esp-nn/`，删除
本地软链接，并把相应构建逻辑作为可复用 framework backend 提交。迁移前不得把
`apps/mlearning/esp-nn/esp-nn` 的跨仓链接提交到公共仓。

### 4.3 IDF 配置适配

ESP-NN 上游通过 `CONFIG_IDF_TARGET_ESP32S3` 和 `CONFIG_NN_OPTIMIZED` 选择后端。
openvela 应使用自身的架构配置，例如 `CONFIG_ARCH_CHIP_ESP32S3`，实现一个很小的
兼容层或 vendor patch 来选择 `ARCH_ESP32_S3`。

禁止为了绕过适配而把 ESP-IDF 的所有配置宏硬编码进全局编译选项。ESP-NN 中仅用于
IDF heap 完整性检查的可选代码应关闭；内核核心不应依赖 ESP-IDF allocator。

## 5. Kconfig 设计

在 [TFLM Kconfig](../../../apps/mlearning/tflite-micro/Kconfig) 的 `if TFLITEMICRO`
作用域新增 backend 选项。符号名建议为：

```kconfig
config TFLITEMICRO_ESP_NN
	bool "Enable ESP-NN optimized kernels"
	default n
	depends on ARCH_CHIP_ESP32S3
	depends on !XTENSA_HIFI
	depends on !MLEARNING_CMSIS_NN
	help
	  Replace supported int8 TFLite Micro kernels with ESP-NN kernels.
	  Unsupported tensor types or parameters use the TFLM reference fallback.
```

在 `ccf_audioevent/board/esp32s3-devkit/configs/` 中使用独立 ESP-NN 专用 profile，并加入：

```text
CONFIG_TFLITEMICRO_ESP_NN=y
```

不要修改默认生产 defconfig，直到数值回归和真机基准全部通过。当前验证 profile 为
`ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_verify/defconfig`。

## 6. CMake 集成

修改 [TFLM CMakeLists](../../../apps/mlearning/tflite-micro/CMakeLists.txt)，在
`CONFIG_TFLITEMICRO_ESP_NN` 分支完成以下工作：

1. 将 `ESP_NN_DIR` 设为 `apps/mlearning/esp-nn/esp-nn`，并在不是目录时以明确的
   `FATAL_ERROR` 提示先创建 `ccf_audioevent` 的本地软链接；
2. 显式列出 ESP-NN 的通用 C 源、ESP32-S3 C 源和 ESP32-S3 `.S` 源；
3. 为 ESP-NN 加入 `include/`、`src/common/` 头文件路径；
4. 不从应用 Makefile 向全局 `CFLAGS` 注入 Xtensa 专用 `-mlongcalls`；由 ESP32-S3
   板级交叉工具链处理目标调用范围，避免污染生成 NuttX host 工具时使用的宿主 GCC；
5. 加入 `-DESP_NN`，供 TFLM wrapper 选择加速路径；
6. 从 `TFLITE_MICRO_SRCS` 排除通用 `conv.cc`、`depthwise_conv.cc`；
7. 加入 openvela 自己维护的 ESP-NN wrapper 源文件。

建议将 wrapper 放在 TFLM 外层，避免改写每次获取的 upstream TFLM 目录：

```text
apps/mlearning/tflite-micro/kernels/esp_nn/
├── conv.cc
└── depthwise_conv.cc
```

伪代码如下；真实文件名应以引入的 ESP-NN commit 为准：

```cmake
if(CONFIG_TFLITEMICRO_ESP_NN)
  set(ESP_NN_DIR ${CMAKE_CURRENT_LIST_DIR}/../esp-nn/esp-nn)
  if(NOT IS_DIRECTORY ${ESP_NN_DIR})
    message(FATAL_ERROR
      "ESP-NN source is unavailable. Create apps/mlearning/esp-nn/esp-nn "
      "as the ccf_audioevent local symlink described in its porting guide.")
  endif()

  list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX
       ".*/tensorflow/lite/micro/kernels/conv.cc$")
  list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX
       ".*/tensorflow/lite/micro/kernels/depthwise_conv.cc$")

  list(APPEND TFLITE_MICRO_SRCS
       ${CMAKE_CURRENT_LIST_DIR}/kernels/esp_nn/conv.cc
       ${CMAKE_CURRENT_LIST_DIR}/kernels/esp_nn/depthwise_conv.cc
       ${ESP_NN_SOURCES})
  list(APPEND INCDIR ${ESP_NN_DIR}/include ${ESP_NN_DIR}/src/common)
  list(APPEND COMMON_FLAGS -DESP_NN -mlongcalls)
endif()
```

ESP-NN 源直接并入 `tflite_micro` 静态库可避免静态库链接顺序导致的未解析符号问题。
如果改为独立库，必须明确链接顺序并在最终 ELF 中检查 `esp_nn_*` 符号确实被解析。

## 7. Conv2D / DepthwiseConv2D wrapper 要求

### 7.1 Prepare 阶段

wrapper 需要从 `TfLiteTensor` 读取 NHWC 输入、filter 和 output 的维度，沿用 TFLM
既有的量化参数计算。之后：

1. 组装 ESP-NN 的 `data_dims_t`、`conv_params_t` 或 `dw_conv_params_t`；
2. 调用 `esp_nn_get_conv_scratch_size()` 或
   `esp_nn_get_depthwise_conv_scratch_size()`；
3. 当返回值大于零时，调用 `RequestScratchBufferInArena()`；
4. 在 node data 中保存 scratch buffer index；
5. 保留通用 TFLM 所需的 per-channel multiplier、shift、padding 与 int4 解包 scratch。

ESP-NN scratch 必须来自 Tensor Arena，不得在 `Eval()` 中 `malloc()`。ESP-NN S3 dispatcher
会以内部静态指针保存本次 Invoke 的 Arena scratch；wrapper 必须在每次 Eval 前重新设置它，
且同一个 interpreter 不得并发 Invoke。模型 Arena 配置要以 `AllocateTensors()` 后的实际
占用重新评估。

### 7.2 Eval 阶段

仅在满足下列条件时调用 ESP-NN：

- input、filter、output 都为 `kTfLiteInt8`；
- bias 缺失或为 `kTfLiteInt32`；
- filter 为 int8，不是 int4；
- dilation width/height 都为 1；
- 当前 ESP-NN commit 支持该 stride、padding、depth multiplier 组合。

调用前由 `context->GetScratchBuffer()` 取得对应 Arena 区域，并调用
`esp_nn_set_conv_scratch_buf()` 或 `esp_nn_set_depthwise_conv_scratch_buf()`。
传递参数时必须完全使用 TFLM 已计算的值：

```text
input offset, output offset, stride, padding, activation min/max,
per-channel output multiplier, per-channel output shift
```

任何不支持的 tensor 类型或参数组合都必须回退 TFLM reference integer 实现；不能静默
改变 padding、量化或 activation 来“适配” ESP-NN。

### 7.3 不要直接复制 ESP-IDF wrapper

Espressif 的 `esp-tflite-micro` wrapper 是有价值的语义参考，但应针对当前 openvela
锁定的 TFLM 版本进行小范围移植。不要整体替换 TFLM，也不要导入其中的
`esp_timer_get_time()` 计时逻辑；openvela 的剖析由 TFLM/NuttX 现有工具完成。

### 7.4 模型白名单与逐元素验证

`CONFIG_TFLITEMICRO_ESP_NN=y` 只表示 ESP-NN 源和受控 wrapper 被构建；Conv2D 和
DepthwiseConv2D 默认仍使用 TFLM reference kernel。先在 benchmark profile 中打开
`CONFIG_TFLITEMICRO_ESP_NN_TRACE=y`，收集每个算子的 tensor ID、NHWC shape、卷积参数和
input/filter/output/scratch 地址对齐信息。

Conv2D 只能通过 output tensor ID 白名单启用：

```text
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR=<tensor-id>
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_VERIFY=y
```

`-1` 是默认值，代表不启用任何 ESP-NN Conv2D。默认白名单只接受 batch=1、int8、
非 group convolution、dilation=1、1x1、stride=1、padding=0、输入通道为 8 的倍数，且
input/output/scratch 地址满足内核对齐要求。选中的节点会把常量 filter 复制到一块 8 字节
对齐的 persistent buffer，再传给 ESP-NN；这避免 FlatBuffer 内的权重起始地址未对齐导致
SIMD 访问异常。该副本只在白名单实际选中节点时分配，原始模型和 reference kernel 均不变。

`CONFIG_TFLITEMICRO_ESP_NN_CONV2D_GENERAL=y` 是第二级、默认关闭的受控放宽。它仍要求
batch=1、int8、非 group convolution、dilation=1 和单一 output tensor 白名单，但将 kernel、
stride、padding 与输入通道数交给 ESP32-S3 dispatcher 判断。它只能与
`CONFIG_TFLITEMICRO_ESP_NN_CONV2D_VERIFY=y` 一起用于 bring-up，不能直接用于生产模型。

旧 4-class 模型的首层验证目标为 `out_t=23`：

```text
input       [1, 49, 40, 3]
filter      [12, 5, 5, 3]
output      [1, 25, 20, 12]
filter_row_size = 5 * 3 = 15  (< 16)
window_len      = 5 * 5 * 3 = 75 (>= 16)
```

S3 dispatcher 因此使用 general Conv 的 im2col 路径：每个 5x5x3 窗口复制为连续 75-byte
向量，尾部补零至 80，再使用 ACCX SIMD dot product。该层的 scratch 约为 1.2 KB；打开
VERIFY 还会申请 6,000 B 的 reference output scratch。专用 profile
`tflm_benchmark_espnn_verify` 已显式设置：

```text
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR=23
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_GENERAL=y
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_VERIFY=y
```

成功条件不是只看到 `esp-nn enter`，而是对零输入、非零固定特征和真实音频特征均出现
`[espnn-verify] Conv2D out_t=23 match`，连续 Invoke 无卡死、无 arena 分配失败。当前
`out_t=23`、`out_t=25` 与 `out_t=27` 已分别完成该类单节点验证；每次验证只选择一个节点，
其余 Conv2D 继续使用 reference backend。

启用 `VERIFY` 时，wrapper 先把 reference 结果写入独立的 Tensor Arena scratch，再运行
ESP-NN 并逐字节比较输出。若不一致，会打印首个差异并恢复 reference 输出，使后续算子
继续使用正确结果。

DepthwiseConv2D 使用独立白名单，不能复用 Conv2D 的配置：

```text
CONFIG_TFLITEMICRO_ESP_NN_DEPTHWISE_CONV2D_OUTPUT_TENSOR=<tensor-id>
CONFIG_TFLITEMICRO_ESP_NN_DEPTHWISE_CONV2D_VERIFY=y
```

第一阶段验证的 `out_t=26` 输入/输出均为 `[1,25,20,16]`，filter 为 `[1,3,3,16]`，精确对应
ESP32-S3 的 16-channel padded 3x3 s8 汇编路径。它已通过逐字节与重复 Invoke 验证，Prepare
阶段申请 9,664 B ESP-NN scratch，VERIFY 再申请 8,000 B reference output。

第二阶段验证 `out_t=24`：输入/输出均为 `[1,25,20,12]`，filter 为 `[1,3,3,12]`，其余参数同为
depth multiplier=1、stride=1、padding=1、dilation=1。该分支将 12 通道补齐至 16 通道，使用
ESP-NN padded s16 mult1 实现并压缩回 12 通道输出；它不是 16-channel s8 汇编快路径。Prepare
阶段申请 24,496 B ESP-NN scratch，VERIFY 再申请 6,000 B reference output；运行时仍要求
input、output 和 scratch 都为 16-byte 对齐。

验证 profile 将 Conv2D 白名单设为 `-1`，仅打开以下 Depthwise 节点：

```text
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR=-1
CONFIG_TFLITEMICRO_ESP_NN_DEPTHWISE_CONV2D_OUTPUT_TENSOR=24
CONFIG_TFLITEMICRO_ESP_NN_DEPTHWISE_CONV2D_VERIFY=y
```

成功标志为 `[espnn-verify] DepthwiseConv2D out_t=24 match bytes=6000`，并在重复 Invoke 中无
卡死或协处理器异常。为保持单节点验证，`out_t=26` 在本阶段恢复 reference backend；只有
`out_t=24` 通过后才讨论累计启用。

### 7.5 组合性能 profile

单节点验证完成后，不能用 `CONV2D_OUTPUT_TENSOR` 同时选择多个 Conv2D。为此新增
`CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR_MASK`：bit N 表示 output tensor ID 为 N 的
Conv2D。它与原有的单节点配置取并集，默认值 `0x0` 不增加任何节点。

`tflm_benchmark_espnn_profile` 是独立的性能 profile，第一阶段仅启用已验证的三个 Conv2D：

```text
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR=-1
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR_MASK=0x0a800000
CONFIG_TFLITEMICRO_ESP_NN_DEPTHWISE_CONV2D_OUTPUT_TENSOR=-1
```

其中 `0x0a800000 = (1 << 23) | (1 << 25) | (1 << 27)`。该 profile 不启用任何 `VERIFY`，因此
测得的是 ESP-NN 的实际执行时间，而不是“reference + ESP-NN + 比较”的 bring-up 时间。通过
重复 Invoke 稳定性测试后，再依次将 `out_t=26` 与 `out_t=24` 加入组合 profile；每加一个阶段
都应记录 Arena 占用、完整原始日志和各算子平均耗时。

性能 profile 也不启用 `CONFIG_TFLITEMICRO_ESP_NN_TRACE`。TRACE 会在每次 ESP-NN 调用时打印
enter/return 地址，串口输出会进入算子计时范围；需要复核节点选择时使用 verify profile，而不应
用 TRACE 版本的 tick 作为性能结论。

### 7.6 三 Conv2D 组合结果

使用上述 profile 在 ESP32-S3 上完成 `--warmup 10 --repeat 50`。三个 Conv2D 均进入 ESP-NN，
两个 DepthwiseConv2D 保持 reference；50 次运行无卡死、重启或协处理器异常。尾部第 49、50 次
的算子记录一致如下：

| 算子 | reference 阶段典型耗时 | 三 Conv2D 组合尾部观测 | 说明 |
| --- | ---: | ---: | --- |
| Conv2D `out_t=23` | 约 162 ms | 0 ms | 小于 10 ms 计时分辨率 |
| DepthwiseConv2D `out_t=24` | 约 24 ms | 30 ms | reference |
| Conv2D `out_t=25` | 约 39 ms | 0 ms | 小于 10 ms 计时分辨率 |
| DepthwiseConv2D `out_t=26` | 约 32 ms | 30 ms | reference |
| Conv2D `out_t=27` | 约 75 ms | 10 ms | 已启用 ESP-NN |
| Mean | 约 12 ms | 10 ms | reference |
| 整次 Invoke | 约 345 ms | 约 80 ms | 第 49、50 次均为约 80 ms |

这组结果表明，在当前 10 ms 计时粒度下，三 Conv2D 已不再是主要瓶颈，组合时延相对 reference
基线约下降 77%（约 4.3 倍吞吐提升）。表中的 0 ms 不能解释为零成本，只能解释为小于一个计时
刻度；不能将其作为正式的百分比结论。下一阶段优先用 CCOUNT 输出正式的 reference 对照数据，
再决定是否加入已验证的 `out_t=26` DepthwiseConv2D。

### 7.7 CCOUNT 正式比较：整次 Invoke 与每算子周期

ESP32-S3 的 `CCOUNT` 是每 CPU 周期递增的 32 位计数器。配置
`CONFIG_TFLITEMICRO_ESP32S3_CCOUNT_PROFILER=y` 后，基准程序有两层计时：

1. `--mode invoke` 在量化输入已复制到 input tensor 后、`Invoke()` 前后读取 CCOUNT；它测量整次
   模型执行，不包含输入量化、输入拷贝、串口打印或统计；
2. TFLM `MicroProfiler` 的 `GetCurrentTimeTicks()` 改由 CCOUNT 提供。`--mode operator --csv`
   输出的每个算子 `Ticks` 因而是 CPU 周期，不再是 10 ms 调度刻度。

为避免输入、频率或编译选项影响结论，使用一对独立配置：

| 配置 | backend | 目的 |
| --- | --- | --- |
| `tflm_benchmark_espnn_cycles_ref` | 所有节点 reference；仍链接 ESP-NN wrapper | 公平 reference 对照 |
| `tflm_benchmark_espnn_cycles` | 仅 Conv2D `out_t=23/25/27` 使用 ESP-NN | 三 Conv2D 组合结果 |

两者均固定 `CONFIG_ESP32S3_DEFAULT_CPU_FREQ_240=y`、`CONFIG_XTENSA_CP_INITSET=0x0009`，并关闭
TRACE/VERIFY。`_ref` 仍启用 `TFLITEMICRO_ESP_NN`，以保证除节点选择外，TFLM wrapper、链接代码
和编译条件相同。

分别构建、烧录后，在同一供电、同一串口波特率和相同温度条件下运行：

```sh
./ccf_audioevent/scripts/link_esp_nn.sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles_ref -j8
# 烧录 reference 固件
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100

./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_cycles -j8
# 烧录 ESP-NN 固件
tflm_benchmark --mode invoke --input pattern --warmup 20 --repeat 100
```

`pattern` 是程序内置、确定性的非零 int8 向量；它确保两个固件执行相同输入，也避免全零输入的
特殊路径掩盖问题。输出中的 `output_hash` 必须在 reference 与 ESP-NN 之间相同，并在同一固件的
所有记录轮次中保持不变。正式报告记录 `invoke_cycles` 的 mean 和 P95，并按
`speedup = reference_mean_cycles / espnn_mean_cycles` 计算加速比；不要只比较单次最小值。

对通过上述总时延检查的固件，再抓取每算子周期：

```sh
tflm_benchmark --mode operator --warmup 10 --repeat 1 --csv
```

该命令的 CSV `Ticks` 单位为 cycle。若要汇总均值/P95，在同一固件上重复采集原始 CSV，并按算子
序号聚合；总 Invoke 仍以 `--mode invoke` 的独立 CCOUNT 结果为准，不能把多个算子 cycle 简单相加
替代它。

## 8. 构建和可观测性检查

先通过本地脚本建立链接，再构建 reference 与 ESP-NN 两套固件：

~~~
./ccf_audioevent/scripts/link_esp_nn.sh
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark_espnn_verify -j8
~~~

该 make profile 显式选择 `CONFIG_XTENSA_TOOLCHAIN_ESP=y`，以确保 `-mlongcalls` 由
`xtensa-esp32s3-elf-gcc` 而非宿主 `gcc` 接收。

该 profile 使用旧 4-class 模型而非 BC-ResNet 预检模型。烧录后先执行：

```sh
tflm_benchmark --warmup 0 --repeat 1 --csv
```

确认首层显示 `candidate=1 selected=1`，并且出现 `out_t=23 match` 后，再进行多次重复
与非零输入回归；不要在首次验证中选择其他 Conv2D 或打开 Depthwise ESP-NN。

ESP-NN 构建完成后，检查：

```sh
# 在构建输出目录执行，路径按实际目标调整
xtensa-esp32s3-elf-nm -C nuttx | rg 'esp_nn_(conv|depthwise)_s8'
```

并核对编译命令中包含 ESP-NN 的 `.S` 文件和 `-mlongcalls`。只有出现 ESP-NN 符号并不
足够；算子级 profile 还必须显示 `Conv2D`、`DepthwiseConv2D` 真实降时。

profile 固件的构建和板端命令见
[tflm_benchmark 算子剖析](../使用与调试/tflm_benchmark算子剖析.md)。生产端到端性能应
继续使用无 OLED 的 P1 profile，不能把 debug/profile 固件的耗时当成生产结果。

## 9. 验证矩阵与准入标准

| 层级 | 输入 | 必须检查 | 准入标准 |
| --- | --- | --- | --- |
| ESP-NN kernel | 随机 int8 tensor、边界 zero-point/shift | Conv/DW 输出 | 与 ANSI/reference 逐元素一致 |
| TFLM wrapper | 支持与不支持的参数组合 | fallback、Arena scratch、错误处理 | 支持组合正确加速；不支持组合正确回退 |
| 模型回归 | 固定特征、真实 WAV | 输出 int8、类别、概率 | 输出一致；若上游声明非 bit-exact，则最大差异不超过预设容差 |
| 真机算子剖析 | 同一模型、同一频率、同一 Arena | 每类算子 mean/P95 | Conv/DW 降时且无异常 P95 |
| 端到端测试 | DS-A01 / P1 与 P2 | TP/FP/FN、feature/infer/total、实时系数 | 不降低已定义事件指标，或明确登记取舍 |

记录时必须同时保存模型 SHA-256、固件 SHA-256、ESP-NN commit、完整 defconfig 和原始
串口日志。不得只报告平均 `Invoke()` 时间。

## 10. 常见失败模式

| 现象 | 首先检查 |
| --- | --- |
| 配置阶段报告 ESP-NN source is unavailable | 是否已按第 4.1 节创建 `apps/mlearning/esp-nn/esp-nn` 的本地软链接 |
| 链接时 `Register_CONV_2D` 重复定义 | 是否同时编进通用和 ESP-NN wrapper 的 `conv.cc` |
| 固件能运行但速度无变化 | wrapper 是否真的调用 `esp_nn_*`，及 profile 是否仍显示 reference kernel 占时 |
| `AllocateTensors()` 失败 | ESP-NN scratch 是否已使 Arena 超出配置；读取实际需要字节数后再增大 Arena |
| 输出分类明显变化 | zero-point 的符号、shift/mult 顺序、padding 填充值、activation min/max 是否与 TFLM 一致 |
| 仅某些模型崩溃 | 对 dilation、int4 filter、depth multiplier 或不支持 shapes 是否遗漏 fallback |
| 编译汇编失败 | 是否仅对 ESP32-S3 选中 S3 `.S` 源，且工具链与 `-mlongcalls` 参数一致 |
| 同一模型偶发异常 | 是否使用了全局 scratch 或并发调用了会设置全局 scratch pointer 的 ESP-NN API |

## 11. 实施提交建议

按下列顺序拆分提交，便于回滚和审查：

1. 导入锁定版本的 ESP-NN 源码与许可证说明；
2. 增加 Kconfig/CMake，但默认关闭；
3. 接入 `Conv2D` wrapper 与单算子数值测试；
4. 接入 `DepthwiseConv2D` wrapper 与单算子数值测试；
5. 增加 benchmark profile、模型回归和真机结果；
6. 只有 profile 证明其值得时，再增加 FC、Mean 或其他 kernel。
7. 若证明对多个模型通用，再将源码从 `ccf_audioevent/third_party/esp-nn` 正式迁入
   `apps/mlearning/esp-nn/esp-nn`，移除本地软链接。

每一步都应能够独立构建；`CONFIG_TFLITEMICRO_ESP_NN=n` 时，产物和行为必须保持
reference backend 的原状。

## 12. 当前实现与验证状态

- 已实现 TFLITEMICRO_ESP_NN：仅依赖 ESP32-S3，且与 CMSIS-NN、Xtensa HiFi 互斥；
- CMake 和 Make 构建均编入 ESP-NN 的上游 ESP32-S3 源文件清单及 Xtensa 汇编，并对
  ESP-NN 源应用 -O2 -fno-unroll-loops -mlongcalls；
- 两个 wrapper 移除了 ESP-IDF 的 esp_timer 依赖，应用层与模型代码未修改；
- benchmark profile 支持 ESP-NN 算子 shape/对齐 trace、Conv2D 单层 output-tensor
  白名单和 shadow-reference 逐字节验证；默认值保持全部卷积 reference；
- 已用 xtensa-esp32s3-elf-gcc/g++ -fsyntax-only 验证两个 wrapper、全部 ESP32-S3
  ESP-NN C 源和汇编源；
- 完整 CMake 固件构建需在不影响其他工作的干净 NuttX 构建环境中执行。当前工作区有
  既存 Make 构建残留，CMake 按 NuttX 规则要求先 make distclean；不要在未确认前
  清除该工作区的构建状态。
