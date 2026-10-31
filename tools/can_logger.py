# -*- coding: utf-8 -*-
"""
CAN 总线抓包 + 统计脚本（配合 USB-CAN 分析仪做"旁路真值"）
用法：
    python can_logger.py                          # 默认 PCAN_USBBUS1 @ 500k
    python can_logger.py -c PCAN_USBBUS2 -b 500000
    python can_logger.py -o test.csv -t 60        # 录 60 秒
快捷键：Ctrl+C 停止，自动打印统计并保存 CSV。
"""
import argparse
import csv
import sys
import time
from collections import Counter

try:
    import can
except ImportError:
    print("python-can 未安装，先运行：pip install python-can")
    sys.exit(1)


def parse_args():
    p = argparse.ArgumentParser(description="CAN 抓包 + 统计")
    p.add_argument("-i", "--interface", default="pcan", help="接口，默认 pcan")
    p.add_argument("-c", "--channel", default="PCAN_USBBUS1", help="通道")
    p.add_argument("-b", "--bitrate", type=int, default=500000, help="波特率，默认 500k")
    p.add_argument("-o", "--out", default="can_log.csv", help="CSV 输出路径")
    p.add_argument("-t", "--duration", type=float, default=0, help="运行秒数，0=一直跑")
    return p.parse_args()


def main():
    args = parse_args()

    try:
        bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)
    except Exception as e:
        print("[FAIL] 打开失败：", repr(e))
        print("先跑 python can_check.py 排查")
        sys.exit(1)

    print(f"[OK] {args.channel} @ {args.bitrate // 1000} kbps   ->  {args.out}")
    print("     Ctrl+C 停止\n")

    f = open(args.out, "w", newline="", encoding="utf-8")
    w = csv.writer(f)
    w.writerow(["time_s", "dir", "id_hex", "dlc", "data_hex", "type"])

    total = 0
    per_id = Counter()
    t0 = time.time()
    last_report = t0
    last_total = 0

    try:
        while True:
            if args.duration and (time.time() - t0) >= args.duration:
                break

            msg = bus.recv(timeout=0.2)
            if msg is None:
                continue

            total += 1
            per_id[msg.arbitration_id] += 1
            ts = time.time() - t0
            w.writerow([
                f"{ts:.6f}",
                "RX",
                f"0x{msg.arbitration_id:03X}",
                msg.dlc,
                msg.data.hex().upper(),
                "EXT" if msg.is_extended_id else "STD",
            ])

            now = time.time()
            if now - last_report >= 1.0:
                fps = (total - last_total) / (now - last_report)
                print(f"  t={now - t0:6.1f}s  累计 {total:>8d}  当前 {fps:6.1f} 帧/s  "
                      f"ID 种类 {len(per_id)}")
                f.flush()
                last_report = now
                last_total = total

    except KeyboardInterrupt:
        print("\n[停止]")
    finally:
        elapsed = time.time() - t0
        f.close()
        bus.shutdown()

    print("\n" + "=" * 46)
    print(f"总帧数   : {total}")
    print(f"总时长   : {elapsed:.1f} s")
    if elapsed > 0:
        print(f"平均帧率 : {total / elapsed:.1f} 帧/s")
    print(f"ID 种类  : {len(per_id)}")
    if per_id:
        print("出现最多的 5 个 ID:")
        for cid, cnt in per_id.most_common(5):
            print(f"  0x{cid:03X}  {cnt:>8d} 帧  ({cnt / max(total, 1) * 100:.1f}%)")
    print(f"\nCSV 已保存: {args.out}")


if __name__ == "__main__":
    main()
