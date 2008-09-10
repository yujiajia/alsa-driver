/*
 * Based on sound/arm/pxa2xx-ac97.c and sound/soc/pxa/pxa2xx-ac97.c
 * which contain:
 *
 * Author:	Nicolas Pitre
 * Created:	Dec 02, 2004
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <sound/ac97_codec.h>
#include <sound/pxa2xx-lib.h>

#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/pxa-regs.h>
#include <mach/pxa2xx-gpio.h>
#include <mach/audio.h>

static DEFINE_MUTEX(car_mutex);
static DECLARE_WAIT_QUEUE_HEAD(gsr_wq);
static volatile long gsr_bits;
static struct clk *ac97_clk;
static struct clk *ac97conf_clk;

/*
 * Beware PXA27x bugs:
 *
 *   o Slot 12 read from modem space will hang controller.
 *   o CDONE, SDONE interrupt fails after any slot 12 IO.
 *
 * We therefore have an hybrid approach for waiting on SDONE (interrupt or
 * 1 jiffy timeout if interrupt never comes).
 */

unsigned short pxa2xx_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	unsigned short val = -1;
	volatile u32 *reg_addr;

	mutex_lock(&car_mutex);

	/* set up primary or secondary codec space */
	if ((cpu_is_pxa21x() || cpu_is_pxa25x()) && reg == AC97_GPIO_STATUS)
		reg_addr = ac97->num ? &SMC_REG_BASE : &PMC_REG_BASE;
	else
		reg_addr = ac97->num ? &SAC_REG_BASE : &PAC_REG_BASE;
	reg_addr += (reg >> 1);

	/* start read access across the ac97 link */
	GSR = GSR_CDONE | GSR_SDONE;
	gsr_bits = 0;
	val = *reg_addr;
	if (reg == AC97_GPIO_STATUS)
		goto out;
	if (wait_event_timeout(gsr_wq, (GSR | gsr_bits) & GSR_SDONE, 1) <= 0 &&
	    !((GSR | gsr_bits) & GSR_SDONE)) {
		printk(KERN_ERR "%s: read error (ac97_reg=%d GSR=%#lx)\n",
				__func__, reg, GSR | gsr_bits);
		val = -1;
		goto out;
	}

	/* valid data now */
	GSR = GSR_CDONE | GSR_SDONE;
	gsr_bits = 0;
	val = *reg_addr;
	/* but we've just started another cycle... */
	wait_event_timeout(gsr_wq, (GSR | gsr_bits) & GSR_SDONE, 1);

out:	mutex_unlock(&car_mutex);
	return val;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_read);

void pxa2xx_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
			unsigned short val)
{
	volatile u32 *reg_addr;

	mutex_lock(&car_mutex);

	/* set up primary or secondary codec space */
	if ((cpu_is_pxa21x() || cpu_is_pxa25x()) && reg == AC97_GPIO_STATUS)
		reg_addr = ac97->num ? &SMC_REG_BASE : &PMC_REG_BASE;
	else
		reg_addr = ac97->num ? &SAC_REG_BASE : &PAC_REG_BASE;
	reg_addr += (reg >> 1);

	GSR = GSR_CDONE | GSR_SDONE;
	gsr_bits = 0;
	*reg_addr = val;
	if (wait_event_timeout(gsr_wq, (GSR | gsr_bits) & GSR_CDONE, 1) <= 0 &&
	    !((GSR | gsr_bits) & GSR_CDONE))
		printk(KERN_ERR "%s: write error (ac97_reg=%d GSR=%#lx)\n",
				__func__, reg, GSR | gsr_bits);

	mutex_unlock(&car_mutex);
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_write);

#ifdef CONFIG_PXA25x
static inline void pxa_ac97_warm_pxa25x(void)
{
	gsr_bits = 0;

	GCR |= GCR_WARM_RST | GCR_PRIRDY_IEN | GCR_SECRDY_IEN;
	wait_event_timeout(gsr_wq, gsr_bits & (GSR_PCR | GSR_SCR), 1);
}

static inline void pxa_ac97_cold_pxa25x(void)
{
	GCR &=  GCR_COLD_RST;  /* clear everything but nCRST */
	GCR &= ~GCR_COLD_RST;  /* then assert nCRST */

	gsr_bits = 0;

	GCR = GCR_COLD_RST;
	GCR |= GCR_CDONE_IE|GCR_SDONE_IE;
	wait_event_timeout(gsr_wq, gsr_bits & (GSR_PCR | GSR_SCR), 1);
}
#endif

#ifdef CONFIG_PXA27x
static inline void pxa_ac97_warm_pxa27x(void)
{
	gsr_bits = 0;

	/* warm reset broken on Bulverde,
	   so manually keep AC97 reset high */
	pxa_gpio_mode(113 | GPIO_OUT | GPIO_DFLT_HIGH);
	udelay(10);
	GCR |= GCR_WARM_RST;
	pxa_gpio_mode(113 | GPIO_ALT_FN_2_OUT);
	udelay(500);
}

static inline void pxa_ac97_cold_pxa27x(void)
{
	GCR &=  GCR_COLD_RST;  /* clear everything but nCRST */
	GCR &= ~GCR_COLD_RST;  /* then assert nCRST */

	gsr_bits = 0;

	/* PXA27x Developers Manual section 13.5.2.2.1 */
	clk_enable(ac97conf_clk);
	udelay(5);
	clk_disable(ac97conf_clk);
	GCR = GCR_COLD_RST;
	udelay(50);
}
#endif

#ifdef CONFIG_PXA3xx
static inline void pxa_ac97_warm_pxa3xx(void)
{
	int timeout = 100;

	gsr_bits = 0;

	/* Can't use interrupts */
	GCR |= GCR_WARM_RST;
	while (!((GSR | gsr_bits) & (GSR_PCR | GSR_SCR)) && timeout--)
		mdelay(1);
}

static inline void pxa_ac97_cold_pxa3xx(void)
{
	int timeout = 1000;

	/* Hold CLKBPB for 100us */
	GCR = 0;
	GCR = GCR_CLKBPB;
	udelay(100);
	GCR = 0;

	GCR &=  GCR_COLD_RST;  /* clear everything but nCRST */
	GCR &= ~GCR_COLD_RST;  /* then assert nCRST */

	gsr_bits = 0;

	/* Can't use interrupts on PXA3xx */
	GCR &= ~(GCR_PRIRDY_IEN|GCR_SECRDY_IEN);

	GCR = GCR_WARM_RST | GCR_COLD_RST;
	while (!(GSR & (GSR_PCR | GSR_SCR)) && timeout--)
		mdelay(10);
}
#endif

bool pxa2xx_ac97_try_warm_reset(struct snd_ac97 *ac97)
{
#ifdef CONFIG_PXA25x
	if (cpu_is_pxa21x() || cpu_is_pxa25x())
		pxa_ac97_warm_pxa25x();
	else
#endif
#ifdef CONFIG_PXA27x
	if (cpu_is_pxa27x())
		pxa_ac97_warm_pxa27x();
	else
#endif
#ifdef CONFIG_PXA3xx
	if (cpu_is_pxa3xx())
		pxa_ac97_warm_pxa3xx();
	else
#endif
		BUG();

	if (!((GSR | gsr_bits) & (GSR_PCR | GSR_SCR))) {
		printk(KERN_INFO "%s: warm reset timeout (GSR=%#lx)\n",
				 __func__, gsr_bits);

		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_try_warm_reset);

bool pxa2xx_ac97_try_cold_reset(struct snd_ac97 *ac97)
{
#ifdef CONFIG_PXA25x
	if (cpu_is_pxa21x() || cpu_is_pxa25x())
		pxa_ac97_cold_pxa25x();
	else
#endif
#ifdef CONFIG_PXA27x
	if (cpu_is_pxa27x())
		pxa_ac97_cold_pxa27x();
	else
#endif
#ifdef CONFIG_PXA3xx
	if (cpu_is_pxa3xx())
		pxa_ac97_cold_pxa3xx();
	else
#endif
		BUG();

	if (!((GSR | gsr_bits) & (GSR_PCR | GSR_SCR))) {
		printk(KERN_INFO "%s: cold reset timeout (GSR=%#lx)\n",
				 __func__, gsr_bits);

		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_try_cold_reset);


void pxa2xx_ac97_finish_reset(struct snd_ac97 *ac97)
{
	GCR &= ~(GCR_PRIRDY_IEN|GCR_SECRDY_IEN);
	GCR |= GCR_SDONE_IE|GCR_CDONE_IE;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_finish_reset);

static irqreturn_t pxa2xx_ac97_irq(int irq, void *dev_id)
{
	long status;

	status = GSR;
	if (status) {
		GSR = status;
		gsr_bits |= status;
		wake_up(&gsr_wq);

		/* Although we don't use those we still need to clear them
		   since they tend to spuriously trigger when MMC is used
		   (hardware bug? go figure)... */
		if (cpu_is_pxa27x()) {
			MISR = MISR_EOC;
			PISR = PISR_EOC;
			MCSR = MCSR_EOC;
		}

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

#ifdef CONFIG_PM
int pxa2xx_ac97_hw_suspend(void)
{
	GCR |= GCR_ACLINK_OFF;
	clk_disable(ac97_clk);
	return 0;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_hw_suspend);

int pxa2xx_ac97_hw_resume(void)
{
	if (cpu_is_pxa21x() || cpu_is_pxa25x() || cpu_is_pxa27x()) {
		pxa_gpio_mode(GPIO31_SYNC_AC97_MD);
		pxa_gpio_mode(GPIO30_SDATA_OUT_AC97_MD);
		pxa_gpio_mode(GPIO28_BITCLK_AC97_MD);
		pxa_gpio_mode(GPIO29_SDATA_IN_AC97_MD);
	}
	if (cpu_is_pxa27x()) {
		/* Use GPIO 113 as AC97 Reset on Bulverde */
		pxa_gpio_mode(113 | GPIO_ALT_FN_2_OUT);
	}
	clk_enable(ac97_clk);
	return 0;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_hw_resume);
#endif

int __devinit pxa2xx_ac97_hw_probe(struct platform_device *dev)
{
	int ret;

	ret = request_irq(IRQ_AC97, pxa2xx_ac97_irq, 0, "AC97", NULL);
	if (ret < 0)
		goto err;

	if (cpu_is_pxa21x() || cpu_is_pxa25x() || cpu_is_pxa27x()) {
		pxa_gpio_mode(GPIO31_SYNC_AC97_MD);
		pxa_gpio_mode(GPIO30_SDATA_OUT_AC97_MD);
		pxa_gpio_mode(GPIO28_BITCLK_AC97_MD);
		pxa_gpio_mode(GPIO29_SDATA_IN_AC97_MD);
	}

	if (cpu_is_pxa27x()) {
		/* Use GPIO 113 as AC97 Reset on Bulverde */
		pxa_gpio_mode(113 | GPIO_ALT_FN_2_OUT);
		ac97conf_clk = clk_get(&dev->dev, "AC97CONFCLK");
		if (IS_ERR(ac97conf_clk)) {
			ret = PTR_ERR(ac97conf_clk);
			ac97conf_clk = NULL;
			goto err_irq;
		}
	}

	ac97_clk = clk_get(&dev->dev, "AC97CLK");
	if (IS_ERR(ac97_clk)) {
		ret = PTR_ERR(ac97_clk);
		ac97_clk = NULL;
		goto err_irq;
	}

	return clk_enable(ac97_clk);

err_irq:
	GCR |= GCR_ACLINK_OFF;
	if (ac97conf_clk) {
		clk_put(ac97conf_clk);
		ac97conf_clk = NULL;
	}
	free_irq(IRQ_AC97, NULL);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_hw_probe);

void pxa2xx_ac97_hw_remove(struct platform_device *dev)
{
	GCR |= GCR_ACLINK_OFF;
	free_irq(IRQ_AC97, NULL);
	if (ac97conf_clk) {
		clk_put(ac97conf_clk);
		ac97conf_clk = NULL;
	}
	clk_disable(ac97_clk);
	clk_put(ac97_clk);
	ac97_clk = NULL;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_hw_remove);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("Intel/Marvell PXA sound library");
MODULE_LICENSE("GPL");

