/**
  ******************************************************************************
  * @file    can_proto.c
  * @brief   CAN-GW v1 应用层协议实现：组帧、解帧、CRC8
  ******************************************************************************
  */

#include "can_proto.h"

/**
  * @brief  CRC-8/ATM
  *         多项式 0x07，初值 0x00，不反射，不异或
  *         逐位计算，不需要查表（一个字节 8 次循环，72 MHz 下可忽略）
  * @param  data 数据首地址
  * @param  len  字节数
  * @retval 8 位校验值
  */
uint8_t cangw_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00u;
    uint8_t i;
    uint8_t b;

    for (i = 0u; i < len; i++)
    {
        crc ^= data[i];
        for (b = 0u; b < 8u; b++)
        {
            if ((crc & 0x80u) != 0u)
            {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }

    return crc;
}

/**
  * @brief  组帧：[type][seq][D0..D4][crc8]
  * @param  buf     输出缓冲，长度必须 >= 8
  * @param  type    类型码
  * @param  seq     序号
  * @param  payload 5 字节负载
  */
void cangw_pack(uint8_t *buf, uint8_t type, uint8_t seq, const uint8_t *payload)
{
    uint8_t i;

    buf[0] = type;
    buf[1] = seq;

    for (i = 0u; i < CANGW_PAYLOAD_LEN; i++)
    {
        buf[2u + i] = payload[i];
    }

    /* CRC 覆盖前 7 字节，自己放在第 8 字节 */
    buf[7] = cangw_crc8(buf, 7u);
}

/**
  * @brief  校验一帧
  * @retval 1 = 通过，0 = 失败
  */
int cangw_check(const uint8_t *buf)
{
    return (buf[7] == cangw_crc8(buf, 7u)) ? 1 : 0;
}

/**
  * @brief  解帧：拆到结构体里，调用前应先 cangw_check()
  */
void cangw_unpack(const uint8_t *buf, cangw_frame_t *frame)
{
    uint8_t i;

    frame->type = buf[0];
    frame->seq  = buf[1];

    for (i = 0u; i < CANGW_PAYLOAD_LEN; i++)
    {
        frame->payload[i] = buf[2u + i];
    }

    frame->crc = buf[7];
}
