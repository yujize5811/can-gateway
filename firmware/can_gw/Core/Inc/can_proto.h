/**
  ******************************************************************************
  * @file    can_proto.h
  * @brief   CAN-GW v1 应用层协议：帧格式与 CRC8
  ******************************************************************************
  * 帧格式（8 字节定长）：
  *
  *   buf[0]  type     报文类型
  *   buf[1]  seq      序号，0~255 循环
  *   buf[2]  D0       payload[0]
  *   buf[3]  D1       payload[1]
  *   buf[4]  D2       payload[2]
  *   buf[5]  D3       payload[3]
  *   buf[6]  D4       payload[4]
  *   buf[7]  crc8     对 buf[0..6] 计算
  *
  * CRC-8 参数（CRC-8/ATM）：
  *   多项式 0x07，初值 0x00，输入输出不反射，结果不异或
  *   校验模型可写 "CRC-8/POLY=0x07/INIT=0x00/REFIN=false/REFOUT=false/XOROUT=0x00"
  *
  * 类型占位（完整分段见 docs/02-protocol.md）：
  *   0x1x  心跳      ID 0x1xx
  *   0x2x  周期数据  ID 0x2xx
  *   0x3x  命令      ID 0x3xx
  *   0x4x  多帧分片  ID 0x4xx
  *   0x5x  ACK       ID 0x5xx
  ******************************************************************************
  */

#ifndef __CAN_PROTO_H__
#define __CAN_PROTO_H__

#include <stdint.h>

/* 帧长固定为 8 字节，正好一个 CAN 数据场 */
#define CANGW_FRAME_LEN     8u
#define CANGW_PAYLOAD_LEN   5u

/* 类型码高半字节，与 ID 分段一一对应 */
#define CANGW_TYPE_HEARTBEAT  0x10u
#define CANGW_TYPE_DATA       0x21u
#define CANGW_TYPE_CMD        0x30u
#define CANGW_TYPE_BLOCK      0x40u
#define CANGW_TYPE_ACK        0x50u

/* 节点 ID：0x200 + 节点号 */
#define CANGW_ID_BASE       0x200u

typedef struct
{
    uint8_t type;
    uint8_t seq;
    uint8_t payload[CANGW_PAYLOAD_LEN];
    uint8_t crc;
} cangw_frame_t;

uint8_t cangw_crc8(const uint8_t *data, uint8_t len);

void cangw_pack(uint8_t *buf, uint8_t type, uint8_t seq, const uint8_t *payload);

int cangw_check(const uint8_t *buf);

void cangw_unpack(const uint8_t *buf, cangw_frame_t *frame);

#endif /* __CAN_PROTO_H__ */
