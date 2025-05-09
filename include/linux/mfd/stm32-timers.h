/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics 2016
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com>
 */

#ifndef _LINUX_STM32_GPTIMER_H_
#define _LINUX_STM32_GPTIMER_H_

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/regmap.h>

#define TIM_CR1		0x00			/* Control Register 1			*/
#define TIM_CR2		0x04			/* Control Register 2			*/
#define TIM_SMCR	0x08			/* Slave mode control reg		*/
#define TIM_DIER	0x0C			/* DMA/interrupt register		*/
#define TIM_SR		0x10			/* Status register			*/
#define TIM_EGR		0x14			/* Event Generation Reg			*/
#define TIM_CCMR1	0x18			/* Capt/Comp 1 Mode Reg			*/
#define TIM_CCMR2	0x1C			/* Capt/Comp 2 Mode Reg			*/
#define TIM_CCER	0x20			/* Capt/Comp Enable Reg			*/
#define TIM_CNT		0x24			/* Counter				*/
#define TIM_PSC		0x28			/* Prescaler				*/
#define TIM_ARR		0x2c			/* Auto-Reload Register			*/
#define TIM_CCRx(x)	(0x34 + 4 * ((x) - 1))	/* Capt/Comp Register x (x ∈ {1, .. 4})	*/
#define TIM_CCR1	TIM_CCRx(1)		/* Capt/Comp Register 1			*/
#define TIM_CCR2	TIM_CCRx(2)		/* Capt/Comp Register 2			*/
#define TIM_CCR3	TIM_CCRx(3)		/* Capt/Comp Register 3			*/
#define TIM_CCR4	TIM_CCRx(4)		/* Capt/Comp Register 4			*/
#define TIM_BDTR	0x44			/* Break and Dead-Time Reg		*/
#define TIM_DCR		0x48			/* DMA control register			*/
#define TIM_DMAR	0x4C			/* DMA register for transfer		*/
#define TIM_TISEL	0x68			/* Input Selection			*/
#define TIM_HWCFGR2	0x3EC			/* hardware configuration 2 Reg (MP25)	*/
#define TIM_HWCFGR1	0x3F0			/* hardware configuration 1 Reg (MP25)	*/
#define TIM_IPIDR	0x3F8			/* IP identification Reg (MP25)		*/

#define TIM_CR1_CEN		BIT(0)					/* Counter Enable				*/
#define TIM_CR1_DIR		BIT(4)					/* Counter Direction				*/
#define TIM_CR1_ARPE		BIT(7)					/* Auto-reload Preload Ena			*/
#define TIM_CR2_MMS		(BIT(4) | BIT(5) | BIT(6))		/* Master mode selection			*/
#define TIM_CR2_MMS2		GENMASK(23, 20)				/* Master mode selection 2			*/
#define TIM_SMCR_SMS		(BIT(0) | BIT(1) | BIT(2))		/* Slave mode selection				*/
#define TIM_SMCR_TS		(BIT(4) | BIT(5) | BIT(6))		/* Trigger selection				*/
#define TIM_DIER_UIE		BIT(0)					/* Update interrupt				*/
#define TIM_DIER_CCxIE(x)	BIT(1 + ((x) - 1))			/* CCx Interrupt Enable	(x ∈ {1, .. 4})		*/
#define TIM_DIER_CC1IE		TIM_DIER_CCxIE(1)			/* CC1 Interrupt Enable				*/
#define TIM_DIER_CC2IE		TIM_DIER_CCxIE(2)			/* CC2 Interrupt Enable				*/
#define TIM_DIER_CC3IE		TIM_DIER_CCxIE(3)			/* CC3 Interrupt Enable				*/
#define TIM_DIER_CC4IE		TIM_DIER_CCxIE(4)			/* CC4 Interrupt Enable				*/
#define TIM_DIER_UDE		BIT(8)					/* Update DMA request Enable			*/
#define TIM_DIER_CCxDE(x)	BIT(9 + ((x) - 1))			/* CCx DMA request Enable (x ∈ {1, .. 4})	*/
#define TIM_DIER_CC1DE		TIM_DIER_CCxDE(1)			/* CC1 DMA request Enable			*/
#define TIM_DIER_CC2DE		TIM_DIER_CCxDE(2)			/* CC2 DMA request Enable			*/
#define TIM_DIER_CC3DE		TIM_DIER_CCxDE(3)			/* CC3 DMA request Enable			*/
#define TIM_DIER_CC4DE		TIM_DIER_CCxDE(4)			/* CC4 DMA request Enable			*/
#define TIM_DIER_COMDE		BIT(13)					/* COM DMA request Enable			*/
#define TIM_DIER_TDE		BIT(14)					/* Trigger DMA request Enable			*/
#define TIM_SR_UIF		BIT(0)					/* Update interrupt flag			*/
#define TIM_SR_CC_IF(x)		BIT((x) + 1)				/* CC1, CC2, CC3, CC4 interrupt flag		*/
#define TIM_EGR_UG		BIT(0)					/* Update Generation				*/
#define TIM_CCMR_PE		BIT(3)					/* Channel Preload Enable			*/
#define TIM_CCMR_M1		(BIT(6) | BIT(5))			/* Channel PWM Mode 1				*/
#define TIM_CCMR_CC1S		(BIT(0) | BIT(1))			/* Capture/compare 1 sel			*/
#define TIM_CCMR_IC1PSC		GENMASK(3, 2)				/* Input capture 1 prescaler			*/
#define TIM_CCMR_CC2S		(BIT(8) | BIT(9))			/* Capture/compare 2 sel			*/
#define TIM_CCMR_IC2PSC		GENMASK(11, 10)				/* Input capture 2 prescaler			*/
#define TIM_CCMR_CC1S_TI1	BIT(0)					/* IC1/IC3 selects TI1/TI3			*/
#define TIM_CCMR_CC1S_TI2	BIT(1)					/* IC1/IC3 selects TI2/TI4			*/
#define TIM_CCMR_CC2S_TI2	BIT(8)					/* IC2/IC4 selects TI2/TI4			*/
#define TIM_CCMR_CC2S_TI1	BIT(9)					/* IC2/IC4 selects TI1/TI3			*/
#define TIM_CCMR_CC3S		(BIT(0) | BIT(1))			/* Capture/compare 3 sel			*/
#define TIM_CCMR_CC4S		(BIT(8) | BIT(9))			/* Capture/compare 4 sel			*/
#define TIM_CCMR_CC3S_TI3	BIT(0)					/* IC3 selects TI3				*/
#define TIM_CCMR_CC4S_TI4	BIT(8)					/* IC4 selects TI4				*/
#define TIM_CCER_CCxE(x)	BIT(0 + 4 * ((x) - 1))			/* Capt/Comp x  out Ena (x ∈ {1, .. 4})		*/
#define TIM_CCER_CCxP(x)	BIT(1 + 4 * ((x) - 1))			/* Capt/Comp x  Polarity (x ∈ {1, .. 4})	*/
#define TIM_CCER_CCxNE(x)	BIT(2 + 4 * ((x) - 1))			/* Capt/Comp xN out Ena (x ∈ {1, .. 4})		*/
#define TIM_CCER_CCxNP(x)	BIT(3 + 4 * ((x) - 1))			/* Capt/Comp xN Polarity (x ∈ {1, .. 4})	*/
#define TIM_CCER_CC1E		TIM_CCER_CCxE(1)			/* Capt/Comp 1  out Ena				*/
#define TIM_CCER_CC1P		TIM_CCER_CCxP(1)			/* Capt/Comp 1  Polarity			*/
#define TIM_CCER_CC1NE		TIM_CCER_CCxNE(1)			/* Capt/Comp 1N out Ena				*/
#define TIM_CCER_CC1NP		TIM_CCER_CCxNP(1)			/* Capt/Comp 1N Polarity			*/
#define TIM_CCER_CC2E		TIM_CCER_CCxE(2)			/* Capt/Comp 2  out Ena				*/
#define TIM_CCER_CC2P		TIM_CCER_CCxP(2)			/* Capt/Comp 2  Polarity			*/
#define TIM_CCER_CC2NE		TIM_CCER_CCxNE(2)			/* Capt/Comp 2N out Ena				*/
#define TIM_CCER_CC2NP		TIM_CCER_CCxNP(2)			/* Capt/Comp 2N Polarity			*/
#define TIM_CCER_CC3E		TIM_CCER_CCxE(3)			/* Capt/Comp 3  out Ena				*/
#define TIM_CCER_CC3P		TIM_CCER_CCxP(3)			/* Capt/Comp 3  Polarity			*/
#define TIM_CCER_CC3NE		TIM_CCER_CCxNE(3)			/* Capt/Comp 3N out Ena				*/
#define TIM_CCER_CC3NP		TIM_CCER_CCxNP(3)			/* Capt/Comp 3N Polarity			*/
#define TIM_CCER_CC4E		TIM_CCER_CCxE(4)			/* Capt/Comp 4  out Ena				*/
#define TIM_CCER_CC4P		TIM_CCER_CCxP(4)			/* Capt/Comp 4  Polarity			*/
#define TIM_CCER_CC4NE		TIM_CCER_CCxNE(4)			/* Capt/Comp 4N out Ena				*/
#define TIM_CCER_CC4NP		TIM_CCER_CCxNP(4)			/* Capt/Comp 4N Polarity			*/
#define TIM_CCER_CCXE		(BIT(0) | BIT(4) | BIT(8) | BIT(12))
#define TIM_BDTR_BKE(x)		BIT(12 + (x) * 12)			/* Break input enable				*/
#define TIM_BDTR_BKP(x)		BIT(13 + (x) * 12)			/* Break input polarity				*/
#define TIM_BDTR_AOE		BIT(14)					/* Automatic Output Enable			*/
#define TIM_BDTR_MOE		BIT(15)					/* Main Output Enable				*/
#define TIM_BDTR_BKF(x)		(0xf << (16 + (x) * 4))
#define TIM_DCR_DBA		GENMASK(4, 0)				/* DMA base addr				*/
#define TIM_DCR_DBL		GENMASK(12, 8)				/* DMA burst len				*/
#define TIM_HWCFGR1_NB_OF_CC	GENMASK(3, 0)				/* Capture/compare channels			*/
#define TIM_HWCFGR1_NB_OF_DT	GENMASK(7, 4)				/* Complementary outputs & dead-time generators */
#define TIM_HWCFGR2_CNT_WIDTH	GENMASK(15, 8)				/* Counter width				*/

#define MAX_TIM_PSC				0xFFFF
#define MAX_TIM_ICPSC				0x3
#define TIM_CR2_MMS_SHIFT			4
#define TIM_CR2_MMS2_SHIFT			20
#define TIM_SMCR_SMS_SLAVE_MODE_DISABLED	0 /* counts on internal clock when CEN=1 */
#define TIM_SMCR_SMS_ENCODER_MODE_1		1 /* counts TI1FP1 edges, depending on TI2FP2 level */
#define TIM_SMCR_SMS_ENCODER_MODE_2		2 /* counts TI2FP2 edges, depending on TI1FP1 level */
#define TIM_SMCR_SMS_ENCODER_MODE_3		3 /* counts on both TI1FP1 and TI2FP2 edges */
#define TIM_SMCR_TS_SHIFT			4
#define TIM_BDTR_BKF_MASK			0xF
#define TIM_BDTR_BKF_SHIFT(x)			(16 + (x) * 4)

#define STM32MP25_TIM_IPIDR	0x00120002

enum stm32_timers_dmas {
	STM32_TIMERS_DMA_CH1,
	STM32_TIMERS_DMA_CH2,
	STM32_TIMERS_DMA_CH3,
	STM32_TIMERS_DMA_CH4,
	STM32_TIMERS_DMA_UP,
	STM32_TIMERS_DMA_TRIG,
	STM32_TIMERS_DMA_COM,
	STM32_TIMERS_MAX_DMAS,
};

/* STM32 Timer may have either a unique global interrupt or 4 interrupt lines */
enum stm32_timers_irqs {
	STM32_TIMERS_IRQ_GLOBAL_BRK, /* global or brk IRQ */
	STM32_TIMERS_IRQ_UP,
	STM32_TIMERS_IRQ_TRG_COM,
	STM32_TIMERS_IRQ_CC,
	STM32_TIMERS_MAX_IRQS,
};

/**
 * struct stm32_timers_dma - STM32 timer DMA handling.
 * @completion:		end of DMA transfer completion
 * @phys_base:		control registers physical base address
 * @lock:		protect DMA access
 * @chan:		DMA channel in use
 * @chans:		DMA channels available for this timer instance
 */
struct stm32_timers_dma {
	struct completion completion;
	phys_addr_t phys_base;
	struct mutex lock;
	struct dma_chan *chan;
	struct dma_chan *chans[STM32_TIMERS_MAX_DMAS];
};

struct stm32_timers {
	struct clk *clk;
	u32 ipidr;
	struct regmap *regmap;
	u32 max_arr;
	struct stm32_timers_dma dma; /* Only to be used by the parent */
	unsigned int nr_irqs;
	int irq[STM32_TIMERS_MAX_IRQS];
};

#if IS_REACHABLE(CONFIG_MFD_STM32_TIMERS)
int stm32_timers_dma_burst_read(struct device *dev, u32 *buf,
				enum stm32_timers_dmas id, u32 reg,
				unsigned int num_reg, unsigned int bursts,
				unsigned long tmo_ms);
#else
static inline int stm32_timers_dma_burst_read(struct device *dev, u32 *buf,
					      enum stm32_timers_dmas id,
					      u32 reg,
					      unsigned int num_reg,
					      unsigned int bursts,
					      unsigned long tmo_ms)
{
	return -ENODEV;
}
#endif
#endif
