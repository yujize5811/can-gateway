/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "can_proto.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 节点 ID：A 板 = 0x201，B 板 = 0x202。
 * 两块板烧同一份代码，只改这一个数字。 */
#define NODE_ID   0x201u
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN_Init();
  /* USER CODE BEGIN 2 */
/* ① 过滤器：必须在 Start 之前配置。
 *    HAL_CAN_ConfigFilter 会先进入 Init 模式再退出，
 *    已经 Start 过再调会返回错误、卡死在 Error_Handler。
 *    掩码全 0 = 不筛，全部接收。 */
CAN_FilterTypeDef canFilter;
canFilter.FilterBank           = 0;
canFilter.FilterMode           = CAN_FILTERMODE_IDMASK;
canFilter.FilterScale          = CAN_FILTERSCALE_32BIT;
canFilter.FilterIdHigh         = 0x0000;
canFilter.FilterIdLow          = 0x0000;
canFilter.FilterMaskIdHigh     = 0x0000;
canFilter.FilterMaskIdLow      = 0x0000;
canFilter.FilterFIFOAssignment = CAN_RX_FIFO0;
canFilter.FilterActivation     = ENABLE;
canFilter.SlaveStartFilterBank = 14;
if (HAL_CAN_ConfigFilter(&hcan, &canFilter) != HAL_OK)
{
    Error_Handler();
}

/* ② 启动 CAN */
if (HAL_CAN_Start(&hcan) != HAL_OK)
{
    Error_Handler();
}

/* ③ 板载 LED：PC13，低电平点亮 */
__HAL_RCC_GPIOC_CLK_ENABLE();
GPIO_InitTypeDef ledGpio = {0};
ledGpio.Pin   = GPIO_PIN_13;
ledGpio.Mode  = GPIO_MODE_OUTPUT_PP;
ledGpio.Pull  = GPIO_NOPULL;
ledGpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &ledGpio);

CAN_TxHeaderTypeDef txHeader;
CAN_RxHeaderTypeDef rxHeader;
uint32_t txMailbox = 0;
uint8_t  rxData[8] = {0};
uint8_t  rxCount = 0;   /* 调试器观察用：等于 rxOk 的低 8 位 */
uint16_t rxOk  = 0;     /* CRC 通过的帧数 */
uint16_t rxErr = 0;     /* CRC 失败的帧数 */
uint8_t  seq   = 0;     /* 发送序号，0~255 循环 */
uint8_t  lastSeq = 0;   /* 上一次收到的对方 seq，用于算丢帧 */
uint16_t lostCnt = 0;   /* 累计丢帧数（按 seq 缺口推算） */
uint16_t reSync  = 0;   /* 对方重启/大跳变次数，不计入丢帧 */
uint8_t  payload[5] = {0};
uint8_t  txBuf[8]   = {0};
uint16_t cnt = 0;     /* 已发出的数据帧数 */

/* ---- T8：心跳与离线检测 ---- */
uint32_t lastRxTick = 0;   /* 上一次收到对方合法帧的时刻（ms） */
uint32_t lastHbTick = 0;   /* 上一次发心跳的时刻（ms） */
uint16_t offlineCnt = 0;   /* 进入离线状态的次数（按跳变计，不按秒累加） */
uint8_t  offline    = 0;   /* 1 = 已经 3 秒没收到对方 */
uint8_t  hbSeq      = 0;   /* 心跳自己的序号，与数据帧分开 */
uint8_t  hbPayload[5] = {0};
uint8_t  hbBuf[8]     = {0};

/* ---- T7：Bus-Off 检测 ---- */
uint16_t busOffCnt  = 0;   /* 进入 Bus-Off 的次数（按 0->1 跳变计，不按轮数累加） */
uint8_t  busOffLast = 0;   /* 上一轮读到的 BOFF 位 */
uint8_t  busOffNow  = 0;   /* 本轮读到的 BOFF 位 */

txHeader.StdId = NODE_ID;
txHeader.ExtId = 0;
txHeader.IDE   = CAN_ID_STD;
txHeader.RTR   = CAN_RTR_DATA;
txHeader.DLC   = 8;
txHeader.TransmitGlobalTime = DISABLE;

/* 心跳用独立的 ID：节点号 - 0x100。
 * A 板数据 0x201 / 心跳 0x101；B 板数据 0x202 / 心跳 0x102。
 * 分开的好处：PCAN-View 里两类帧各占一行，互不覆盖，
 * 一眼就能看出"数据在跑但心跳停了"还是"两个都停了"。 */
CAN_TxHeaderTypeDef hbHeader = txHeader;
hbHeader.StdId = (uint16_t)(NODE_ID - 0x100u);

/* ---- T5：多帧块传输（1 KB）----
 * 发送端：START（带总长）→ 256 个 FRAG（4 字节/帧）→ END（带整包 CRC32）
 * 接收端：按 fragIdx 往 blkRxBuf 里填，END 到了再算一遍 CRC32 对比。
 * 三个类型走独立的 0x3xx 车道，和数据 0x2xx 分开。 */
#define BLK_LEN      1024u
#define BLK_FRAMES   (BLK_LEN / CANGW_BLK_CHUNK)   /* 256 */

CAN_TxHeaderTypeDef blkHeader = txHeader;
blkHeader.StdId = (uint16_t)(NODE_ID + 0x100u);   /* A 0x301 / B 0x302 */

uint8_t  blkBuf[8]     = {0};
uint8_t  blkPayload[5] = {0};

/* 发送侧 */
static uint8_t blkTxBuf[BLK_LEN] = {0};   /* 待发的 1 KB —— 必须 static，
                                           * 放栈上会直接爆掉默认 1 KB 的栈 */
uint8_t  blkActive  = 0;            /* 1 = 正在发这个块 */
uint16_t blkTxIdx   = 0;            /* 下一个要发的分片号 */
uint32_t blkNextAt  = 0;            /* 下一次开始传输的时刻 */
uint8_t  blkId      = 0;            /* 块编号，每传完一次 +1 */
uint8_t  blkSeq     = 0;            /* 块帧自己的序号 */
uint32_t blkCrcTx   = 0;

/* 接收侧 */
static uint8_t blkRxBuf[BLK_LEN] = {0};   /* 同样必须 static */
uint8_t  blkRxActive = 0;
uint8_t  blkRxId     = 0;
uint16_t blkRxLen    = 0;
uint16_t blkRxExpect = 0;           /* 期望的下一个分片号 */
uint16_t blkRxGot    = 0;           /* 实际收到多少个分片 */
uint16_t blkOkCnt    = 0;           /* 整包校验通过的次数 */
uint16_t blkFailCnt  = 0;           /* 整包校验失败的次数 */

/* ACK 帧：整包校验完立刻回一帧，把结果摊在 PCAN-View 上。
 * 走 0x5xx 车道（A 0x501 / B 0x502），和数据、块传输都分开。 */
CAN_TxHeaderTypeDef ackHeader = txHeader;
CAN_TxHeaderTypeDef nakHeader = txHeader;
uint8_t  ackPayload[5] = {0};
uint8_t  ackBuf[8]     = {0};

/* ---- T6：NACK（缺片请求重传，回退 N 帧） ---- */
uint8_t  nakPayload[5] = {0};
uint8_t  nakBuf[8]     = {0};
uint8_t  nakSeq        = 0;
uint16_t nakCnt        = 0;      /* 本机总共发出过多少次 NACK */
uint32_t nakLastTick   = 0;      /* 上次发 NACK 的时刻，用于限速 */
uint16_t blkRetryCnt   = 0;      /* 本机作为发送端，被要求重传的次数 */
uint8_t  ackSeq        = 0;
ackHeader.StdId = (uint16_t)(NODE_ID + 0x300u);   /* A 0x501 / B 0x502 */
nakHeader.StdId = (uint16_t)(NODE_ID + 0x400u);   /* A 0x601 / B 0x602 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* ---- 接收：CRC 校验 + 计数 ----
     * 用 while 而不是 if：一轮里把 FIFO 中所有待处理的帧全部取完。
     * FIFO 只有 3 级深，如果每轮只取 1 帧，一旦某轮被发送阻塞拖长
     * （仲裁失败重发、HAL_Delay 抖动），就会溢出丢帧。 */
    while (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK)
    {
      if (rxHeader.StdId != NODE_ID)
      {
        if (cangw_check(rxData))
        {
          rxOk++;                     /* 校验通过 */
          lastRxTick = HAL_GetTick(); /* 收到任何合法帧都说明对方还活着 */

          /* ---- T5：块传输的三个分支 ---- */
          if (rxData[0] == CANGW_TYPE_BLK_START)
          {
            blkRxActive = 1u;
            blkRxId     = rxData[2];
            blkRxLen    = (uint16_t)(((uint16_t)rxData[3] << 8) | rxData[4]);
            blkRxExpect = 0u;
            blkRxGot    = 0u;
            continue;
          }

          if (rxData[0] == CANGW_TYPE_BLK_NAK)
          {
            /* 对方说：我缺片了，从我那个 startIdx 开始重发。
             * 我们这边作为发送端，把发送指针退回去即可 —— 这就是
             * Go-Back-N 的全部实现：不做选择重传，退回去全发一遍。 */
            if ((blkActive != 0u) && (rxData[2] == blkId))
            {
              uint16_t start = rxData[3];

              if (start < BLK_FRAMES)
              {
                blkTxIdx = start;      /* 回退发送指针 */
                blkRetryCnt++;
              }
            }
            continue;
          }

          if (rxData[0] == CANGW_TYPE_BLK_FRAG)
          {
            uint16_t idx = rxData[2];
            uint16_t off = (uint16_t)(idx * CANGW_BLK_CHUNK);
            uint8_t  k;

            /* 只接受"期望中的那一片"。乱序或重复都丢掉——
             * 先做最笨的版本，等做到 T6 滑窗再放宽容错。 */
            if ((blkRxActive != 0u) && (idx == blkRxExpect) && (off + CANGW_BLK_CHUNK <= BLK_LEN))
            {
              for (k = 0u; k < CANGW_BLK_CHUNK; k++)
              {
                blkRxBuf[off + k] = rxData[3u + k];
              }
              blkRxExpect++;
              blkRxGot++;
            }
            else if ((blkRxActive != 0u) && (idx > blkRxExpect))
            {
              /* 序号往前跳了 —— 中间那几片没收到。
               * 回一帧 NACK，请对端从 blkRxExpect 开始重发。
               *
               * 为什么必须限速：对端每次重发都会再次触发这个判断，
               * 不限速就会「NACK 刷屏 + FRAG 重发」互相叠加，
               * 总线瞬间打满，比不重传还糟。50 ms 是经验值：
               * 一帧 FRAG 只要 0.23 ms，50 ms 足够对端发完 200 多帧。 */
              uint32_t nakNow = HAL_GetTick();

              if ((nakNow - nakLastTick) >= 50u)
              {
                nakPayload[0] = blkRxId;
                nakPayload[1] = (uint8_t)(blkRxExpect & 0xFFu);
                nakPayload[2] = 0u;
                nakPayload[3] = 0u;
                nakPayload[4] = (uint8_t)(nakCnt & 0xFFu);

                cangw_pack(nakBuf, CANGW_TYPE_BLK_NAK, nakSeq, nakPayload);
                if (HAL_CAN_AddTxMessage(&hcan, &nakHeader, nakBuf, &txMailbox) == HAL_OK)
                {
                  nakSeq++;
                  nakCnt++;
                  nakLastTick = nakNow;
                }
              }
            }
            continue;
          }

          if (rxData[0] == CANGW_TYPE_BLK_END)
          {
            if (blkRxActive != 0u)
            {
              uint32_t want = ((uint32_t)rxData[2] << 24) | ((uint32_t)rxData[3] << 16) |
                              ((uint32_t)rxData[4] << 8)  |  (uint32_t)rxData[5];
              uint32_t got  = cangw_crc32(blkRxBuf, blkRxLen);

              if (got == want)
              {
                blkOkCnt++;          /* 整包校验通过 */
              }
              else
              {
                blkFailCnt++;        /* 拼起来的和发出来的不是一份 */
              }

              /* 立刻回一帧 ACK，把结果摊到 PCAN-View 上：
               *   D0 = 块编号    D1 = 0 通过 / 1 失败
               *   D2D3 = 实际收到多少片（小端）   D4 = 累计通过次数 */
              ackPayload[0] = blkRxId;
              ackPayload[1] = (got == want) ? 0u : 1u;
              ackPayload[2] = (uint8_t)(blkRxGot & 0xFFu);
              ackPayload[3] = (uint8_t)((blkRxGot >> 8) & 0xFFu);
              ackPayload[4] = (uint8_t)(blkOkCnt > 0x7Fu ? 0x7Fu : blkOkCnt);

              cangw_pack(ackBuf, CANGW_TYPE_ACK, ackSeq, ackPayload);
              if (HAL_CAN_AddTxMessage(&hcan, &ackHeader, ackBuf, &txMailbox) == HAL_OK)
              {
                ackSeq++;
              }

              blkRxActive = 0u;
            }
            continue;
          }

          /* 只有数据帧参与缺口统计。
           * 心跳走的是另一个 seq，如果混进来会把编号搅乱，
           * 统计出一堆根本不存在的"丢帧"。 */
          if (rxData[0] != CANGW_TYPE_DATA)
          {
            continue;
          }

          /* ---- 丢帧检测：看序号缺口 ----
           * seq 是 uint8_t，0~255 循环。gap = 本次 - 上次 - 1：
           *   gap == 0   → 连续，正常
           *   gap < 128  → 中间缺了 gap 帧（真丢帧）
           *   gap >= 128 → 说明对方重启了（序号回到小值），
           *                不是丢帧，只重新对齐，不计入。
           * 用无符号减法自动处理 255→0 的回绕。 */
          {
            uint8_t s   = rxData[1];
            uint8_t gap = (uint8_t)(s - lastSeq - 1u);

            if (gap == 0u)
            {
              /* 连续，什么都不用做 */
            }
            else if (gap < 128u)
            {
              lostCnt += gap;
            }
            else
            {
              reSync++;
            }

            lastSeq = s;
          }
        }
        else
        {
          rxErr++;         /* 校验失败 */
        }
      }
    }
    rxCount = (uint8_t)rxOk;

    /* ---- 发送：组帧 ----
     * 帧布局（8 字节）：
     *   [0] type
     *   [1] seq
     *   [2][3] 发送计数 cnt（16 位，低字节在前）
     *   [4][5] 丢帧计数 lostCnt（16 位，低字节在前）
     *   [6]    CRC 失败计数 rxErr（低 8 位，正常情况下恒为 0）
     *   [7]    CRC8
     * 上一版把 rxOk 和 lostCnt 都压成 8 位，超过 255 会回绕，
     * 只能看"变没变"、看不出"是多少"，所以改成 16 位。 */
    payload[0] = (uint8_t)(cnt & 0xFF);
    payload[1] = (uint8_t)((cnt >> 8) & 0xFF);
    payload[2] = (uint8_t)(lostCnt & 0xFF);
    payload[3] = (uint8_t)((lostCnt >> 8) & 0xFF);
    payload[4] = (uint8_t)((offline ? 0x80u : 0u) |          /* bit7 = 对方离线 */
                           (rxErr > 0x7Fu ? 0x7Fu : rxErr)); /* bit6..0 = CRC 错次数 */

    cangw_pack(txBuf, CANGW_TYPE_DATA, seq, payload);

    if (HAL_CAN_AddTxMessage(&hcan, &txHeader, txBuf, &txMailbox) == HAL_OK)
    {
      cnt++;
      seq++;      /* 只有真正发出去的帧才占用序号。
                   * 否则发送失败时 seq 空跳，对端会算成漏帧——
                   * 这叫"幻影丢帧"，是自己制造的假故障。 */
    }

    /* ---- T8：离线判定 + 心跳 ----
     * 离线判定：距离上次收到合法帧超过 3000 ms 就认为对方离线。
     * 阈值取心跳周期的 3 倍——丢 1~2 个心跳不至于误判，
     * 真断了 3 秒内一定能发现。
     * 注意：这里只记「状态跳变次数」，不按秒累加，
     * 否则数字会随断电时长乱涨，看不出断过几次。 */
    {
      uint32_t age = HAL_GetTick() - lastRxTick;

      if (age > 3000u)
      {
        if (offline == 0u)
        {
          offline = 1u;
          offlineCnt++;
        }
      }
      else
      {
        offline = 0u;      /* 收到帧立刻恢复在线 */
      }

      /* 心跳：每秒一帧。它有自己的 seq，和数据帧的缺口统计互不干扰。 */
      if ((HAL_GetTick() - lastHbTick) >= 1000u)
      {
        uint32_t up = HAL_GetTick() / 1000u;

        lastHbTick += 1000u;

        hbPayload[0] = (uint8_t)(up & 0xFF);
        hbPayload[1] = (uint8_t)((up >> 8) & 0xFF);
        hbPayload[2] = (uint8_t)((CAN1->ESR >> 16) & 0xFF);  /* 临时诊断：TEC 发送错误计数 */
        hbPayload[3] = (uint8_t)(busOffCnt  & 0xFF);  /* 让出的那一字节放 Bus-Off 次数 */
        hbPayload[4] = (uint8_t)((offline   != 0u ? 0x80u : 0u) |   /* bit7 当前是否离线 */
                                 (busOffNow != 0u ? 0x40u : 0u));   /* bit6 当前是否 Bus-Off */

        cangw_pack(hbBuf, CANGW_TYPE_HEARTBEAT, hbSeq, hbPayload);
        if (HAL_CAN_AddTxMessage(&hcan, &hbHeader, hbBuf, &txMailbox) == HAL_OK)
        {
          hbSeq++;
        }
      }
    }

    /* ---- T5：多帧块传输（发 1 KB）----
     * 节奏：每轮循环只发一个块帧（20 ms 一个），所以 258 帧大约 5.4 秒传完。
     * 这是"连发"版本，没有逐帧 ACK，靠最后的 CRC32 判整包对错。
     * 等 T6 把停等 / 滑窗做完，再回来对比耗时。 */
    {
      uint32_t now = HAL_GetTick();

      if ((blkActive == 0u) && ((now - blkNextAt) >= 5000u))
      {
        uint16_t i;

        blkId++;
        for (i = 0u; i < BLK_LEN; i++)
        {
          blkTxBuf[i] = (uint8_t)((i * 7u) + blkId);   /* 可预测，便于对拍 */
        }
        blkCrcTx  = cangw_crc32(blkTxBuf, BLK_LEN);
        blkTxIdx  = 0u;
        blkSeq    = 0u;
        blkActive = 1u;

        blkPayload[0] = blkId;
        blkPayload[1] = (uint8_t)((BLK_LEN >> 8) & 0xFFu);
        blkPayload[2] = (uint8_t)(BLK_LEN & 0xFFu);
        blkPayload[3] = 0u;
        blkPayload[4] = 0u;

        cangw_pack(blkBuf, CANGW_TYPE_BLK_START, blkSeq, blkPayload);
        if (HAL_CAN_AddTxMessage(&hcan, &blkHeader, blkBuf, &txMailbox) == HAL_OK)
        {
          blkSeq++;
        }
      }
      else if (blkActive != 0u)
      {
        if (blkTxIdx < BLK_FRAMES)
        {
          uint16_t off = (uint16_t)(blkTxIdx * CANGW_BLK_CHUNK);

          blkPayload[0] = (uint8_t)(blkTxIdx & 0xFFu);
          blkPayload[1] = blkTxBuf[off + 0u];
          blkPayload[2] = blkTxBuf[off + 1u];
          blkPayload[3] = blkTxBuf[off + 2u];
          blkPayload[4] = blkTxBuf[off + 3u];

          cangw_pack(blkBuf, CANGW_TYPE_BLK_FRAG, blkSeq, blkPayload);
          if (HAL_CAN_AddTxMessage(&hcan, &blkHeader, blkBuf, &txMailbox) == HAL_OK)
          {
            blkSeq++;
            blkTxIdx++;
          }
        }
        else
        {
          /* 所有分片发完 → 发 END，带上整包 CRC32（大端 4 字节） */
          blkPayload[0] = (uint8_t)((blkCrcTx >> 24) & 0xFFu);
          blkPayload[1] = (uint8_t)((blkCrcTx >> 16) & 0xFFu);
          blkPayload[2] = (uint8_t)((blkCrcTx >> 8)  & 0xFFu);
          blkPayload[3] = (uint8_t)( blkCrcTx        & 0xFFu);
          blkPayload[4] = 0u;

          cangw_pack(blkBuf, CANGW_TYPE_BLK_END, blkSeq, blkPayload);
          if (HAL_CAN_AddTxMessage(&hcan, &blkHeader, blkBuf, &txMailbox) == HAL_OK)
          {
            blkSeq++;
            blkActive = 0u;
            blkNextAt = now;      /* 从这一刻起，5 秒后再传下一包 */
          }
        }
      }
    }

    /* ---- T7：Bus-Off 检测 ----
     * CAN1->ESR 的 bit2 = BOFF：进 Bus-Off 时硬件置 1，恢复后自动清 0。
     * 只在 0->1 的跳变时计数 —— 如果数"当前是不是 1"，Bus-Off 持续 100 轮
     * 就会被记成进了 100 次。 */
    busOffNow = (uint8_t)((CAN1->ESR >> 2) & 1u);
    if ((busOffNow != 0u) && (busOffLast == 0u))
    {
      busOffCnt++;

      /* T7：恢复。
       * ABOM 关掉以后，硬件不会自己回来，必须由软件把 CAN 控制器
       * 重新拉回正常模式。HAL_CAN_Start() 内部会清 INRQ 位，
       * 等价于"请求退出初始化模式"，控制器重新参与总线。
       * 备注：这里只做"恢复动作"，恢复成功与否由下一轮 ESR 判断。 */
      (void)HAL_CAN_Start(&hcan);

      /* 恢复后原来的过滤器仍在，不需要重新配置。 */
    }
    busOffLast = busOffNow;

    HAL_Delay(20);   /* 20 ms = 50 帧/秒 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
