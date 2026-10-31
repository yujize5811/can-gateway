# 固件工程说明

## 目录约定

```
firmware/
├── node-a/          发送节点（CAN 数据源 + 多帧发送）
├── node-b/          接收节点（收帧 + 重组 + 统计）
└── common/          协议编解码、CRC、日志（两边共用）
```

每个节点是一个独立的 STM32CubeIDE 工程（含 `.ioc`）。

## 环境

| 项 | 版本 |
|---|---|
| IDE | STM32CubeIDE 1.x（或用 Keil MDK 5 + STM32CubeMX 生成） |
| HAL | STM32Cube FW_F1 |
| 工具链 | arm-none-eabi-gcc |
| 调试器 | ST-Link V2 |

## 编译与烧录

```bash
# 命令行编译（CubeIDE 生成的 Makefile）
cd firmware/node-a
make -j8

# 烧录
st-flash write build/node-a.bin 0x08000000
```

CubeIDE 图形界面：`Project → Build All`，然后 `Run → Debug`。

## 关键配置（必须与 docs/01-can-timing.md 一致）

```c
hcan1.Instance = CAN1;
hcan1.Init.Prescaler = 8;              // BRP
hcan1.Init.Mode = CAN_MODE_NORMAL;
hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
hcan1.Init.TimeSeg1 = CAN_BS1_6TQ;
hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
hcan1.Init.AutoBusOff = DISABLE;       // 手动处理 Bus-Off，便于记录恢复时间
hcan1.Init.AutoRetransmission = ENABLE;
hcan1.Init.ReceiveFifoLocked = DISABLE;
hcan1.Init.TransmitFifoPriority = DISABLE;
```

配完回读验证：

```c
// 期望 0x00150007（不含 SILM/LBKM 位）
printf("BTR = 0x%08lX\n", CAN1->BTR);
```

## 调试技巧

**1. 先回环，再双机**

`CAN_MODE_LOOPBACK` 下自发自收，不依赖第二块板，能单独验证位定时和收发路径。

**2. 单板发送必报错是正常的**

正常模式下没有 ACK 就重传，看 `CAN_ESR.TEC` 一路涨到 255 进 Bus-Off —— 这是协议行为，不是代码 bug。

**3. Bus-Off 恢复**

```c
if (CAN1->ESR & CAN_ESR_BOFF) {
    HAL_CAN_Stop(&hcan1);
    HAL_CAN_Start(&hcan1);   // 重新初始化，比让硬件自动恢复更可控
}
```

**4. 中断里只搬数据**

CAN 接收中断里只把报文搬进环形缓冲区，协议解析放在任务里。中断里做解析会把 P99 延迟拉到毫秒级。

## 编译产物

`build/`、`Debug/`、`*.o`、`*.elf`、`*.map` 全部在 `.gitignore` 里，**不入库**。

需要发版时把 `.hex` 传到 GitHub Release，不提交到仓库。
