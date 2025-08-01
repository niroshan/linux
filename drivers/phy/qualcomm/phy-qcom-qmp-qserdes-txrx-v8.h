/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef QCOM_PHY_QMP_QSERDES_TXRX_V8_H_
#define QCOM_PHY_QMP_QSERDES_TXRX_V8_H_

#define QSERDES_V8_TX_TX_EMP_POST1_LVL			0x00c
#define QSERDES_V8_TX_TX_DRV_LVL			0x014
#define QSERDES_V8_TX_RES_CODE_LANE_TX			0x034
#define QSERDES_V8_TX_RES_CODE_LANE_RX			0x038
#define QSERDES_V8_TX_RES_CODE_LANE_OFFSET_TX		0x03c
#define QSERDES_V8_TX_RES_CODE_LANE_OFFSET_RX		0x040
#define QSERDES_V8_TX_TRANSCEIVER_BIAS_EN		0x054
#define QSERDES_V8_TX_HIGHZ_DRVR_EN			0x058
#define QSERDES_V8_TX_TX_POL_INV			0x05c
#define QSERDES_V8_TX_LANE_MODE_1			0x084
#define QSERDES_V8_TX_LANE_MODE_2			0x088
#define QSERDES_V8_TX_LANE_MODE_3			0x08c
#define QSERDES_V8_TX_LANE_MODE_4			0x090
#define QSERDES_V8_TX_LANE_MODE_5			0x094
#define QSERDES_V8_TX_RCV_DETECT_LVL_2			0x0a4
#define QSERDES_V8_TX_PI_QEC_CTRL			0x0e4

#define QSERDES_V8_RX_UCDR_FO_GAIN			0x008
#define QSERDES_V8_RX_UCDR_SO_GAIN			0x014
#define QSERDES_V8_RX_UCDR_SVS_FO_GAIN			0x020
#define QSERDES_V8_RX_UCDR_FASTLOCK_FO_GAIN		0x030
#define QSERDES_V8_RX_UCDR_SO_SATURATION_AND_ENABLE	0x034
#define QSERDES_V8_RX_UCDR_FASTLOCK_COUNT_LOW		0x03c
#define QSERDES_V8_RX_UCDR_FASTLOCK_COUNT_HIGH		0x040
#define QSERDES_V8_RX_UCDR_PI_CONTROLS			0x044
#define QSERDES_V8_RX_UCDR_SB2_THRESH1			0x04c
#define QSERDES_V8_RX_UCDR_SB2_THRESH2			0x050
#define QSERDES_V8_RX_UCDR_SB2_GAIN1			0x054
#define QSERDES_V8_RX_UCDR_SB2_GAIN2			0x058
#define QSERDES_V8_RX_AUX_DATA_TCOARSE_TFINE		0x060
#define QSERDES_V8_RX_VGA_CAL_CNTRL1			0x0d4
#define QSERDES_V8_RX_VGA_CAL_CNTRL2			0x0d8
#define QSERDES_V8_RX_GM_CAL				0x0dc
#define QSERDES_V8_RX_RX_EQU_ADAPTOR_CNTRL2		0x0ec
#define QSERDES_V8_RX_RX_EQU_ADAPTOR_CNTRL3		0x0f0
#define QSERDES_V8_RX_RX_EQU_ADAPTOR_CNTRL4		0x0f4
#define QSERDES_V8_RX_RX_IDAC_TSETTLE_LOW		0x0f8
#define QSERDES_V8_RX_RX_IDAC_TSETTLE_HIGH		0x0fc
#define QSERDES_V8_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1	0x110
#define QSERDES_V8_RX_SIGDET_ENABLES			0x118
#define QSERDES_V8_RX_SIGDET_CNTRL			0x11c
#define QSERDES_V8_RX_SIGDET_DEGLITCH_CNTRL		0x124
#define QSERDES_V8_RX_RX_MODE_00_LOW			0x15c
#define QSERDES_V8_RX_RX_MODE_00_HIGH			0x160
#define QSERDES_V8_RX_RX_MODE_00_HIGH2			0x164
#define QSERDES_V8_RX_RX_MODE_00_HIGH3			0x168
#define QSERDES_V8_RX_RX_MODE_00_HIGH4			0x16c
#define QSERDES_V8_RX_RX_MODE_01_LOW			0x170
#define QSERDES_V8_RX_RX_MODE_01_HIGH			0x174
#define QSERDES_V8_RX_RX_MODE_01_HIGH2			0x178
#define QSERDES_V8_RX_RX_MODE_01_HIGH3			0x17c
#define QSERDES_V8_RX_RX_MODE_01_HIGH4			0x180
#define QSERDES_V8_RX_DFE_EN_TIMER			0x1a0
#define QSERDES_V8_RX_DFE_CTLE_POST_CAL_OFFSET		0x1a4
#define QSERDES_V8_RX_DCC_CTRL1				0x1a8
#define QSERDES_V8_RX_VTH_CODE				0x1b0
#define QSERDES_V8_RX_SIGDET_CAL_CTRL1			0x1e4
#define QSERDES_V8_RX_SIGDET_CAL_TRIM			0x1f8

#endif
