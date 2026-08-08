# ESP-NN 移植问题与解决方案记录

> 本文件记录 ESP-NN 移植到 openvela/TFLite Micro 过程中遇到的全部问题与解决方案，
> 按"移植接入、构建集成、算子稳定性、性能验证"四个阶段整理。每项均为
> **问题 → 根因 → 解决** 三段式，可直接作为排障手册复用。
>
> 背景：目标平台 ESP32-S3（Xtensa LX7），TFLM 已静态集成，模型为全 int8
> DS-CNN（4-class：`3→12→16→24`；8-class：`3→32→64→96`）。
> 设计约束：**不修改 `third_party/esp-nn` 上游源码**，加速逻辑全部放在 TFLM 适配层。

---

## 一、移植接入阶段

### 1. ESP-NN 以嵌套 Git 仓库存在，无法随作品仓交付

**问题**：拉取 ESP-NN 后 `third_party/` 是嵌套 Git 仓库，`ccf_audioevent` 将其视为未跟踪目录，提交时内容丢失。

**解决**：转为普通 vendored 源码（删除嵌套仓库 `.git`，仅删元数据不动源文件），并固定版本：

- 固定 commit `10b6c0fc884a3b05f94a752f91d00ebadfe5d8d0`；
- 新增 `third_party/esp-nn/UPSTREAM.md`（上游 URL、SHA、许可证、导入日期）；
- `THIRD_PARTY_NOTICES.md` 补充 Apache-2.0 声明；
- 提交后 `git -C third_party/esp-nn status --short` 必须无输出。

### 2. 如何"零修改"接入 ESP-NN

**问题**：直接替换全部 int8 Conv/Depthwise 会不稳定（见下文第 5 项），而散改上游源码又会破坏可追溯性。

**解决**：采用"**薄 wrapper + 逐算子白名单**"架构，所有改动只落在 TFLM 适配层：

```text
ccf_audioevent/third_party/esp-nn/        # 固定版本、保持原样
apps/mlearning/tflite-micro/
  ├── Kconfig / CMakeLists.txt / Makefile  # 接入改动
  └── kernels/esp_nn/                      # conv.cc / depthwise_conv.cc / mean.cc
```

- 仅对 TFLM 目标私有传入 `CONFIG_NN_OPTIMIZED=1`、`CONFIG_IDF_TARGET_ESP32S3=1`
  （用于选择 S3 实现）；**不定义 `CONFIG_IDF_CMAKE`**，关闭其 IDF heap 调试依赖；
- TFLM 新增 `CONFIG_TFLITEMICRO_ESP_NN` 开关，`depends on ARCH_CHIP_ESP32S3`、
  `!XTENSA_HIFI`、`!MLEARNING_CMSIS_NN`，默认关闭，应用代码不改。

### 3. 源码放在哪里、如何被构建找到

**问题**：`ccf_audioevent` 与 `apps` 是两个独立仓库，ESP-NN 源码放作品仓后构建系统找不到。

**解决**：作品仓存源码，工作区用软链接挂载：

```text
ccf_audioevent/third_party/esp-nn
  → apps/mlearning/esp-nn/esp-nn        # link_esp_nn.sh 创建
```

软链接不提交进 `apps` 仓；链接缺失时构建直接报明确错误。

---

## 二、构建集成阶段

### 4. Make 构建报 `undefined reference to 'tflm_benchmark_main'`

**问题**：

```text
LD: nuttx xtensa-esp32s3-elf-ld: libapps.a(builtin_list.c...): undefined reference to `tflm_benchmark_main'
```

**根因**：传统 Make 应用集成缺失。`tflm_benchmark` 源码与独立 Makefile 都存在，
但 `apps/examples/audio_event/` 没有 `Make.defs`，因此未加入 `CONFIGURED_APPS`；
`builtin_list.c` 却从旧 registry 登记了该命令，形成"命令表引用存在、实现目标未编入
`libapps.a`"。CMake 路径不受影响。

**解决**：新增 `app/audio_event/Make.defs`，按 Kconfig 互斥选择构建目录：

```make
ifneq ($(CONFIG_EXAMPLES_AUDIO_EVENT),)
CONFIGURED_APPS += $(APPDIR)/examples/audio_event
endif

ifneq ($(CONFIG_EXAMPLES_TFLM_BENCHMARK),)
CONFIGURED_APPS += $(APPDIR)/examples/audio_event/tflm_benchmark
endif
```

### 5. 修复后 `libapps.a` 中对象不完整（nsh/builtin 丢失）

**问题**：`tflm_benchmark_main` 解决后，`nsh_main`、builtin registry 对象也不在归档中，
`libapps.a` 只剩 3 个 benchmark 对象。

**根因**：增量构建状态不一致——顶层构建重建归档时，其他应用的旧 `.built` 标记
阻止对象重新写入 `libapps.a`。

**解决**：按 NuttX 标准流程清理并重建"当前配置的 apps"归档（不清理 NuttX 配置、不动源码），
将 `audio_event` 与 `tflm_benchmark` 改为同一父 Makefile 按 Kconfig 选源的标准模式。
验证：`nuttx/nuttx` 与 `nuttx.bin` 生成，ELF 同时包含 `tflm_benchmark_main` 与
`esp_nn_conv_s8_esp32s3`。

### 6. 首次构建的 esp-hal-3rdparty 依赖与 mbedtls 兼容问题

**问题**：ESP32-S3 首次构建或 `distclean` 后，`esp-hal-3rdparty` 在构建过程中异步
git clone + patch，可能失败或产生 mbedtls 头文件优先级、spinlock 初始化器不兼容。

**解决**：三个专用脚本，与构建**并行**运行（脚本等待目标文件出现，最多 180 s）：

```bash
bash scripts/fix_box3_mbedtls_header_priority.sh   # Fix 1：先修正头文件优先级
bash scripts/fix_box3_mbedtls_disable_ccm.sh &     # Fix 2：禁用 CCM
bash scripts/fix_box3_spinlock_initializer.sh &    # Fix 3：spinlock 初始化器
```

---

## 三、算子稳定性阶段（核心排障）

### 7. 真机跑 benchmark"卡住不动"——实际上是崩溃（EXCCAUSE=0023）

**问题**：`tflm_benchmark --warmup 0 --repeat 1` 后串口长时间无输出，看起来像卡死。

**根因**：不是正常耗时，而是空地址访问崩溃：

```text
xtensa_user_panic: User Exception: EXCCAUSE=0023
VADDR: 00000000
```

模型初始化已成功（`arena=65536 used=42724`），不是 arena 不足；任务栈约剩 1 KB，
也不是栈溢出。是 `esp_nn_conv_s8()` 调用路径触发异常。

**解决**（分三步收敛）：

1. 先用 ELF 反查崩溃点（`addr2line`），区分"在 ESP-NN 内部"还是"在日志链路"；
2. 将怀疑的卷积分支回退 reference，重新构建、烧录、复跑，观察崩溃点是否转移；
3. 反复迭代，直到锁定全部问题分支。

### 8. 崩溃点在 Conv2D 与 DepthwiseConv2D 之间"转移"

**问题**：回退 Conv2D 后，崩溃转移到 `esp_nn_depthwise_conv_s8()`；再回退
Depthwise 后，又出现在普通 Conv2D 的**另一个**分支。

**根因**：**初版适配层只检查了 int8 数据类型，就全量委派 ESP-NN**。ESP-NN 的
S3 实现是多分支内核，不是任意形状的通用替代：

- 普通 Conv2D 分小通道 im2col、1×1 SIMD、对齐/填充等多条路径；
- Depthwise 依赖 depth multiplier、通道数 8/16 倍数、dilation=1 等条件；
- 部分 SIMD 路径要求 filter 指针按特定边界对齐，而 TFLite FlatBuffer 内的
  常量权重偏移不保证满足。

**解决**：改为"**逐算子白名单委派**"，`out_tensor`（tensor ID）精确选择：

```ini
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_OUTPUT_TENSOR=<out_t>
CONFIG_TFLITEMICRO_ESP_NN_CONV2D_VERIFY=y     # 先验证再测性能
```

- 默认 `out_t=-1`（全 reference，零风险）；
- 只为"已在板上验证过的形状"逐层放开；
- 每个节点先 VERIFY 逐字节比对，匹配后再关 VERIFY 测性能。

### 9. 候选节点的 filter 地址未 8 字节对齐

**问题**：`out_t=27`（`1×1`、stride=1、无 padding、输入 16 通道）形状完全合格，
但 trace 显示 `filter_mod8=4`，不满足 ESP-NN SIMD 路径的对齐要求。

**解决**：在 `Prepare()` 为选中节点分配 **8 字节对齐的 persistent filter 副本**，
只复制该节点的 384 B 权重，ESP-NN 始终读对齐副本。额外内存约 391 B（含对齐余量）。
不动上游源码。

### 10. 长变参 Trace 日志本身导致崩溃

**问题**：为诊断加的长日志（大量 `%08x` 参数）输出到一半就崩溃，栈显示
`VDebugLog/VMicroPrintf`，根本没进 ESP-NN——日志链路本身无法稳定处理长可变参数。

**解决**：诊断日志改为**极短且安全**的格式，只传两个整数参数：

```text
[espnn-trace] Conv2D out_t=27 esp-nn enter valid_mask=0x7f
[espnn-trace] Conv2D out_t=27 esp-nn return
```

- 有 enter 无 return → 崩溃在 ESP-NN 内部；
- valid_mask 某位缺失 → 先修 TFLM 参数映射；
- 两条都有 → 进入 VERIFY 数值比对。

### 11. `TfLiteEvalTensor` 没有 `bytes` 成员（编译错误）

**问题**：开启 `CONV2D_VERIFY` 后编译报错：

```text
kernels/esp_nn/conv.cc:566: error: 'TfLiteEvalTensor' has no member named 'bytes'
```

**根因**：`Prepare()` 用的是 `TfLiteTensor`（有 `bytes`），`Eval()` 阶段 `output`
是精简版 `TfLiteEvalTensor`，该版本 TFLM 没有 `bytes` 成员；VERIFY 分支首次参与编译即暴露。

**解决**：用输出 shape 计算字节数，替换三处 `output->bytes`（比对循环上限、
`memcpy` 恢复 reference 输出的长度、`match bytes=` 日志值）：

```cpp
const size_t output_bytes =
    tflite::micro::GetTensorShape(output).FlatSize() * sizeof(int8_t);
```

### 12. 结论"当前没有可直接调用的 Conv2D"——分优先级逐个适配

**问题**：严格条件下首层/中层 Conv2D 都不可安全加速。

**解决**：按 trace 资格表分级处理：

| 节点 | 形状特征 | 处理 |
| --- | --- | --- |
| `out_t=23` | 5×5、stride=2、有 padding | 通用 im2col 路径，暂不碰（崩溃高发区） |
| `out_t=25` | 1×1，输入通道 12 | 非 8 倍数，只能走非满 SIMD 分支，暂缓 |
| `out_t=27` | 1×1、stride=1、无 padding、16 通道 | 只差 filter 对齐，用对齐副本接入（见第 9 项） |

### 13. Mean 特化：量化参数不能只按数学公式推导

**问题**：Mean（GlobalAveragePooling）是五卷积后的最大热点（约 3,308,138 cycles /
13.784 ms）。按 `input_scale / output_scale / 500` 直接计算
`QuantizeMultiplier` 数值正确，但**定点舍入路径与 TFLM reference 不同**，
可能出现 1 LSB 差异导致 `match bytes=24` 失败。

**解决**：复用 TFLM reference 的等价推导路径，把 `/500` 合入：

```text
mean_shift      = floor(log2(500))（按 TFLM 规则限制范围）
mean_multiplier = (base_multiplier << mean_shift) / 500
mean_out_shift  = base_shift - mean_shift
```

`esp_nn_multiply_by_quantized_mult()` 与 TFLM 双舍入实现兼容，保证逐字节一致。

**边界**：仅优化当前 `[1,H,W,24]`、axis `{1,2}` 的节点，其余 Mean 全部自动回退 reference。

### 14. Mean 性能 profile 看不到验证日志

**问题**：Mean 性能固件 Invoke 已从 30.948 ms 降到 18.533 ms，但没有预期的
`[espnn-trace] Mean` / `[espnn-verify] Mean match` 日志。

**根因**：烧录的是性能 profile（`..._mean`），不是 `..._mean_verify`；且
`arena_used=44356` 更像非 verify 版本（verify 还应额外申请 24 B reference 输出缓冲）。

**解决**：确认烧录 verify 配置（`tflm_benchmark_espnn_cycles_dw24_dw26_mean_verify`）
后重跑。**性能结论只允许来自 TRACE/VERIFY 关闭的 profile**。

---

## 四、性能验证阶段

### 15. `--repeat 20` 文件回放耗时过长

**问题**：`audio_event --file /data/event_5mb.wav --repeat 20 --profile --no-oled`
要跑约 20 分钟，误以为"需要跑 20 个文件"。

**根因**：`--repeat N` 是重复回放**同一文件** N 次；单个约 5 MiB WAV 已含数百个
hop（250 ms），每次回放约 8 分钟。

**解决**：常规 P50/P95 性能样本用 `--repeat 1` 即可；`--repeat 20` 仅用于
约 20 分钟的长稳与积压检查。

### 16. Verify 固件总耗时与 reference 持平 = 没有加速？

**问题**：50 次 reference 基线约 345 ms，VERIFY 固件总耗时也约 350 ms。

**解释**：这是**正常现象**，不是没有加速。VERIFY 模式每个节点实际执行
reference + ESP-NN + 逐字节比对 + trace，ESP-NN 的节省被校验开销抵消；
且当时只有首层走 ESP-NN（约 160 ms → 内部下降），在 10 ms 计时精度下分辨不出来。
**加速结论只认 CCOUNT/`--mode invoke` 的正式性能 profile，不认 VERIFY 固件。**

### 17. 性能数字的口径不统一

**问题**：初始测量把浮点量化、日志等都算进推理时间。

**解决**：建立统一的计时边界与流程：

- 纯模型性能：CCOUNT（周期数），计时边界 = 量化输入已复制后 → `Invoke()` 返回；
- 业务侧：`[profile]` 行的 `feature` / `infer` / `total` 毫秒计时；
- 三阶段流程：**ref（reference 基线）→ verify（逐字节验证）→ cycles（正式统计）**，
  每节点"先验证正确性，再测性能"；
- 正式统计：`--warmup 20 --repeat 100`，输出 min / P50 / mean / P95 / max、
  cycle 和 `output_hash`（与 reference 一致才有效）；
- 每算子热点定位：`--mode operator --warmup 10 --repeat 1 --csv`。

---

## 五、关键经验总结

1. **不修改上游源码，用白名单委派**——所有不稳定都源于"未经形状资格判断就全量委派"。
   `out_tensor` 白名单 + 默认 reference 回退，是零风险接入的正确形态。
2. **一个崩溃点修完往往暴露下一个**——按"反查 ELF → 回退该分支 → 复跑"循环收敛，
   不要一次回退全部。
3. **"卡住"先确认是不是崩溃**——`EXCCAUSE=0023 + VADDR=0` 是空地址访问，
   不是超时或卡死。
4. **验证日志要短**——长变参 `MicroPrintf` 在 openvela 日志链路本身不可靠，
   用固定格式 + `valid_mask` 位掩码。
5. **性能结论只认性能 profile**——TRACE/VERIFY 会抬高耗时（reference + 加速 + 比对），
   数字口径以 `--mode invoke` 的 CCOUNT 与 P50/mean/P95 为准。
6. **Mean 等"简单"算子的量化参数必须复用 reference 推导路径**，否则 1 LSB
   差异会导致逐字节验证失败。

## 六、最终成果对照

| 版本 | Invoke mean | 相对 reference | 关键验证 |
| --- | ---: | ---: | --- |
| reference（全 TFLM） | 346.271 ms | 1.00× | 基线 |
| 五卷积 ESP-NN | 30.948 ms | 11.19× | `output_hash` 一致 |
| 五卷积 + Mean ESP-NN | **17.591 ms** | **19.68×** | 每节点 `match`，Mean `match bytes=24` |
| 业务侧（4-class WAV） | infer 20 ms / total 80–90 ms | < 250 ms hop | 文件回放约 2.8× 实时 |

> 排障工具：ELF 反查用 `addr2line`/`nm`；耗时对比用 CCOUNT；数值一致性用
> VERIFY 与 `output_hash`。相关流程详见
> [Reference与ESP-NN性能对比操作手册](../项目基线/Reference与ESP-NN性能对比操作手册.md)。
