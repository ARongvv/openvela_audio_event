/*
 * Lightweight PCM energy gate for the audio event application.
 *
 * 每个采集 PCM 块先调用 power_gate_process() 更新能量状态；在准备处理
 * 一帧特征和模型推理前，再调用 power_gate_take_inference() 决定本轮是否
 * 放行。关闭能量门控时，接口仍保留，且始终放行推理，便于 P0/P1 对比。
 */

#ifndef __APPS_EXAMPLES_AUDIO_EVENT_POWER_POWER_GATE_H
#define __APPS_EXAMPLES_AUDIO_EVENT_POWER_POWER_GATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct power_gate_stats_s
{
  /* 采集侧统计：已分析的 PCM 块数、其中的活动块数和静音转活动次数。 */

  uint64_t blocks;
  uint64_t active_blocks;
  uint64_t active_entries;

  /* 推理侧统计：实际放行与因静音而跳过的推理窗口数。 */

  uint64_t inference_windows;
  uint64_t skipped_windows;

  /* 当前自适应噪声基线，以及由基线计算出的 RMS / 峰值门限。 */

  uint32_t noise_floor_rms;
  uint32_t rms_threshold;
  uint32_t peak_threshold;

  /* 最近一个 PCM 块的测量值，用于日志观察与参数标定。 */

  uint32_t last_rms;
  uint32_t last_peak;

  /* 校准完成、当前处于活动保持期、以及功能是否编译启用。 */

  bool calibrated;
  bool active;
  bool enabled;
};

struct power_gate_s
{
  /* 对外可读取的运行统计；仅由能量门控模块负责更新。 */

  struct power_gate_stats_s stats;

  /* 校准阶段已累计的样本数，用于确定噪声基线是否稳定。 */

  uint64_t calibration_samples;

  /* 最近一次活动后保持放行的截止采样位置。 */

  uint64_t active_until_samples;

  /* 静音期间下一次强制探测的采样位置，防止长期漏检。 */

  uint64_t next_probe_samples;

  /* 到达强制探测时置位；下一次 take_inference() 消费该标志。 */

  bool probe_pending;
};

/* 初始化门控状态。调用后，校准完成前不会因能量门控跳过推理。 */

void power_gate_init(struct power_gate_s *gate);

/*
 * 提交一个连续 PCM 采集块并更新门控状态。
 *
 * sample_end 是该块结束时刻的单调累计样本位置，不是块内索引；调用方
 * 应在每次采集后递增它，供活动保持和强制探测按真实时间工作。
 */

void power_gate_process(struct power_gate_s *gate, const int16_t *samples,
                        size_t sample_count, uint64_t sample_end);

/*
 * 获取当前窗口是否应执行特征提取和模型推理。
 *
 * 此函数会消费一次待执行的强制探测标记，并同步累计推理/跳过统计；每个
 * 准备送入模型的窗口只能调用一次。sample_end 保留在接口中，便于调用点
 * 与采集时间线对应，并为后续按窗口时间扩展策略预留。
 */

bool power_gate_take_inference(struct power_gate_s *gate,
                               uint64_t sample_end);

/* 复制当前统计快照；gate 或 stats 为空时不执行操作。 */

void power_gate_get_stats(const struct power_gate_s *gate,
                          struct power_gate_stats_s *stats);

#endif /* __APPS_EXAMPLES_AUDIO_EVENT_POWER_POWER_GATE_H */
