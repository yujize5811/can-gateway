# -*- coding: utf-8 -*-
"""
CAN 环境自检脚本
用途：确认 python-can 装好、PCAN 驱动在不在、能不能打开设备
运行：python can_check.py
"""
import sys
import os

print("=" * 52)
print("Python :", sys.version.split()[0])
print("平台   :", sys.platform)
print("=" * 52)

# 1. python-can
try:
    import can
    print("[OK] python-can:", can.__version__)
except ImportError:
    print("[FAIL] python-can 未安装，运行：pip install python-can")
    sys.exit(1)

print("[--] 可用接口:", ", ".join(sorted(can.VALID_INTERFACES)))

# 2. PCANBasic.dll（pcan 后端必须）
dll_candidates = [
    r"C:\Windows\System32\PCANBasic.dll",
    r"C:\Windows\SysWOW64\PCANBasic.dll",
]
found = [p for p in dll_candidates if os.path.exists(p)]
if found:
    print("[OK] PCANBasic.dll:", found[0])
else:
    print("[WARN] 没找到 PCANBasic.dll —— 这不是 python-can 的问题")
    print("       它由卖家给的驱动器安装包 / PCAN-View 安装时写入系统目录。")
    print("       装完驱动再跑这个脚本。")

# 3. 尝试打开设备
if not found:
    print("\n跳过硬测试：先装驱动。")
    sys.exit(0)

try:
    bus = can.Bus(interface="pcan", channel="PCAN_USBBUS1", bitrate=500000)
    print("[OK] 设备已打开：PCAN_USBBUS1 @ 500 kbps")
    print("     现在总线上没有其他节点，发帧会报错，这是正常的。")
    bus.shutdown()
except Exception as e:
    print("[FAIL] 打开失败：", repr(e))
    print("       常见原因：")
    print("       1) 分析仪没插好");
    print("       2) 通道名不对，试试 PCAN_USBBUS1 ~ PCAN_USBBUS4")
    print("       3) 用了 PEAK 官网的新驱动，不认这个设备 -> 换卖家驱动包")
    sys.exit(1)

print("\n全部就绪。下一步：python can_logger.py")
