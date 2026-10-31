#!/usr/bin/env python3
"""分析 can_logger.py 抓下来的 CSV。

输出：
  - 总帧数、采样时长、平均帧率、峰值秒帧率
  - 按 ID 统计帧数 / 周期均值 / 抖动 P99
  - 用序号或周期间隔估算丢帧

用法：
    python analyze_log.py run_001.csv
    python analyze_log.py run_001.csv --period-ms 20
"""

import argparse
import csv
import sys
from collections import defaultdict


def pct(sorted_vals, p):
    """线性插值分位数，sorted_vals 必须已排序且非空。"""
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return float(sorted_vals[0])
    k = (len(sorted_vals) - 1) * p
    lo = int(k)
    hi = min(lo + 1, len(sorted_vals) - 1)
    frac = k - lo
    return sorted_vals[lo] * (1 - frac) + sorted_vals[hi] * frac


def main():
    ap = argparse.ArgumentParser(description="CAN 抓包 CSV 分析")
    ap.add_argument("csv_path", help="can_logger.py 输出的 CSV")
    ap.add_argument("--period-ms", type=float, default=None,
                    help="已知周期（ms）。给了就用它算丢帧，否则用各 ID 的中位间隔")
    ap.add_argument("--top", type=int, default=10, help="每个表显示多少条（默认 10）")
    args = ap.parse_args()

    rows = []
    try:
        with open(args.csv_path, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                try:
                    rows.append((float(r["time_s"]), r["id_hex"],
                                 int(r["dlc"]), r.get("type", "STD")))
                except (KeyError, ValueError):
                    continue
    except FileNotFoundError:
        sys.exit(f"找不到文件：{args.csv_path}")

    if not rows:
        sys.exit("CSV 里没有可用数据（检查表头是否为 time_s,dir,id_hex,dlc,data_hex,type）")

    rows.sort(key=lambda x: x[0])
    t0, t1 = rows[0][0], rows[-1][0]
    duration = max(t1 - t0, 1e-9)

    print("=" * 56)
    print(f"文件        : {args.csv_path}")
    print(f"总帧数      : {len(rows)}")
    print(f"采样时长    : {duration:.3f} s")
    print(f"平均帧率    : {len(rows) / duration:.1f} 帧/s")
    print("=" * 56)

    # 每秒帧数 → 找峰值
    per_sec = defaultdict(int)
    for ts, _, _, _ in rows:
        per_sec[int(ts)] += 1
    if per_sec:
        peak_sec = max(per_sec, key=per_sec.get)
        print(f"峰值秒帧率  : {per_sec[peak_sec]} 帧/s @ t={peak_sec}s")

    # 按 ID 分组
    by_id = defaultdict(list)
    for ts, id_hex, dlc, tp in rows:
        by_id[id_hex].append((ts, dlc, tp))

    print(f"不同 ID 数  : {len(by_id)}")
    print()

    header = f"{'ID':>10}  {'帧数':>7}  {'周期ms':>8}  {'抖动P99':>8}  {'最大间隔':>9}  {'丢帧':>6}"
    print(header)
    print("-" * len(header))

    total_missing = 0
    table = []
    for id_hex, items in by_id.items():
        ts_list = [t for t, _, _ in items]
        gaps = [(b - a) * 1000.0 for a, b in zip(ts_list, ts_list[1:])]
        if gaps:
            gaps_sorted = sorted(gaps)
            median = pct(gaps_sorted, 0.5)
            if args.period_ms:
                # 不同 ID 的周期通常是基准周期的整数倍，
                # 取最接近的中位间隔，避免把 60 ms 的 ID 按 20 ms 误算丢帧
                period = max(1, round(median / args.period_ms)) * args.period_ms
            else:
                period = median
            jitter = pct(gaps_sorted, 0.99) - period
            max_gap = gaps_sorted[-1]
            missing = 0
            if period and period > 0:
                missing = sum(max(0, round(g / period) - 1) for g in gaps)
        else:
            period = jitter = max_gap = 0.0
            missing = 0
        total_missing += missing
        table.append((missing, len(items), id_hex, period, jitter, max_gap))

    table.sort(reverse=True)
    for missing, n, id_hex, period, jitter, max_gap in table[: args.top]:
        print(f"{id_hex:>10}  {n:>7}  {period:>8.2f}  {jitter:>8.2f}  {max_gap:>9.2f}  {missing:>6}")

    if len(table) > args.top:
        print(f"... 还有 {len(table) - args.top} 个 ID 未显示（--top 调整）")

    print("-" * len(header))
    print(f"估计总丢帧  : {total_missing}  (按间隔/周期估算，仅供参考)")
    print()

    # 给一条判断建议
    if total_missing == 0:
        print("结论：没有检测到丢帧，周期稳定。")
    else:
        rate = total_missing / len(rows) * 100
        print(f"结论：估计丢帧率 {rate:.3f} %。", end="")
        if rate < 0.01:
            print("偶发，多半是采样边界或单次仲裁失败，可接受。")
        elif rate < 0.1:
            print("偏低但需关注，看看是否集中在某几个 ID 上。")
        else:
            print("偏高。优先检查 120Ω 终端电阻、共地、波特率是否一致。")


if __name__ == "__main__":
    main()
