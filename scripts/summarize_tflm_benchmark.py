#!/usr/bin/env python3
"""Summarize per-operator CSV records emitted by tflm_benchmark."""

import math
import re
import sys
from collections import defaultdict
from pathlib import Path


ITERATION_RE = re.compile(r"\[tflm_benchmark\] iteration=(\d+)")
EVENT_RE = re.compile(r"^(\d+),([^,]+),(\d+)$")


def percentile_nearest_rank(values, percentile):
    ordered = sorted(values)
    index = max(0, math.ceil(percentile * len(ordered)) - 1)
    return ordered[index]


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} SERIAL_LOG", file=sys.stderr)
        return 2

    rows = defaultdict(list)
    current_iteration = None
    for raw_line in Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        match = ITERATION_RE.search(line)
        if match:
            current_iteration = int(match.group(1))
            continue

        match = EVENT_RE.match(line)
        if match and current_iteration is not None:
            _, tag, ticks = match.groups()
            rows[tag].append(int(ticks))

    if not rows:
        print("No TFLM benchmark CSV rows found. Run tflm_benchmark with --csv.",
              file=sys.stderr)
        return 1

    total_mean_ticks = sum(sum(values) / len(values) for values in rows.values())
    print("operator,samples,mean_us,p50_us,p95_us,mean_share_pct")
    for tag, values in sorted(rows.items(), key=lambda item: sum(item[1]), reverse=True):
        mean = sum(values) / len(values)
        share = 100.0 * mean / total_mean_ticks if total_mean_ticks else 0.0
        print(f"{tag},{len(values)},{mean:.1f},{percentile_nearest_rank(values, 0.50)},"
              f"{percentile_nearest_rank(values, 0.95)},{share:.2f}")

    print(f"TOTAL,,{total_mean_ticks:.1f},,,100.00")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
