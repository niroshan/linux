// SPDX-License-Identifier: GPL-2.0
/*
 * Altera PCIe MSI support
 *
 * Author: Ley Foon Tan <lftan@altera.com>
 *
 * Copyright Altera Corporation (C) 2013-2015. All rights reserved
 */

#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/irq-msi-lib.h>
#include <linux/irqdomain.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define MSI_STATUS		0x0
#define MSI_ERROR		0x4
#define MSI_INTMASK		0x8

#define MAX_MSI_VECTORS		32

struct altera_msi {
	DECLARE_BITMAP(used, MAX_MSI_VECTORS);
	struct mutex		lock;	/* protect "used" bitmap */
	struct platform_device	*pdev;
	struct irq_domain	*inner_domain;
	void __iomem		*csr_base;
	void __iomem		*vector_base;
	phys_addr_t		vector_phy;
	u32			num_of_vectors;
	int			irq;
};

static inline void msi_writel(struct altera_msi *msi, const u32 value,
			      const u32 reg)
{
	writel_relaxed(value, msi->csr_base + reg);
}

static inline u32 msi_readl(struct altera_msi *msi, const u32 reg)
{
	return readl_relaxed(msi->csr_base + reg);
}

static void altera_msi_isr(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct altera_msi *msi;
	unsigned long status;
	u32 bit;
	int ret;

	chained_irq_enter(chip, desc);
	msi = irq_desc_get_handler_data(desc);

	while ((status = msi_readl(msi, MSI_STATUS)) != 0) {
		for_each_set_bit(bit, &status, msi->num_of_vectors) {
			/* Dummy read from vector to clear the interrupt */
			readl_relaxed(msi->vector_base + (bit * sizeof(u32)));

			ret = generic_handle_domain_irq(msi->inner_domain, bit);
			if (ret)
				dev_err_ratelimited(&msi->pdev->dev, "unexpected MSI\n");
		}
	}

	chained_irq_exit(chip, desc);
}

#define ALTERA_MSI_FLAGS_REQUIRED (MSI_FLAG_USE_DEF_DOM_OPS		| \
				   MSI_FLAG_USE_DEF_CHIP_OPS		| \
				   MSI_FLAG_NO_AFFINITY)

#define ALTERA_MSI_FLAGS_SUPPORTED (MSI_GENERIC_FLAGS_MASK		| \
				    MSI_FLAG_PCI_MSIX)

static const struct msi_parent_ops altera_msi_parent_ops = {
	.required_flags		= ALTERA_MSI_FLAGS_REQUIRED,
	.supported_flags	= ALTERA_MSI_FLAGS_SUPPORTED,
	.bus_select_token	= DOMAIN_BUS_PCI_MSI,
	.prefix			= "Altera-",
	.init_dev_msi_info	= msi_lib_init_dev_msi_info,
};
static void altera_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct altera_msi *msi = irq_data_get_irq_chip_data(data);
	phys_addr_t addr = msi->vector_phy + (data->hwirq * sizeof(u32));

	msg->address_lo = lower_32_bits(addr);
	msg->address_hi = upper_32_bits(addr);
	msg->data = data->hwirq;

	dev_dbg(&msi->pdev->dev, "msi#%d address_hi %#x address_lo %#x\n",
		(int)data->hwirq, msg->address_hi, msg->address_lo);
}

static struct irq_chip altera_msi_bottom_irq_chip = {
	.name			= "Altera MSI",
	.irq_compose_msi_msg	= altera_compose_msi_msg,
};

static int altera_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *args)
{
	struct altera_msi *msi = domain->host_data;
	unsigned long bit;
	u32 mask;

	WARN_ON(nr_irqs != 1);
	mutex_lock(&msi->lock);

	bit = find_first_zero_bit(msi->used, msi->num_of_vectors);
	if (bit >= msi->num_of_vectors) {
		mutex_unlock(&msi->lock);
		return -ENOSPC;
	}

	set_bit(bit, msi->used);

	mutex_unlock(&msi->lock);

	irq_domain_set_info(domain, virq, bit, &altera_msi_bottom_irq_chip,
			    domain->host_data, handle_simple_irq,
			    NULL, NULL);

	mask = msi_readl(msi, MSI_INTMASK);
	mask |= 1 << bit;
	msi_writel(msi, mask, MSI_INTMASK);

	return 0;
}

static void altera_irq_domain_free(struct irq_domain *domain,
				   unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct altera_msi *msi = irq_data_get_irq_chip_data(d);
	u32 mask;

	mutex_lock(&msi->lock);

	if (!test_bit(d->hwirq, msi->used)) {
		dev_err(&msi->pdev->dev, "trying to free unused MSI#%lu\n",
			d->hwirq);
	} else {
		__clear_bit(d->hwirq, msi->used);
		mask = msi_readl(msi, MSI_INTMASK);
		mask &= ~(1 << d->hwirq);
		msi_writel(msi, mask, MSI_INTMASK);
	}

	mutex_unlock(&msi->lock);
}

static const struct irq_domain_ops msi_domain_ops = {
	.alloc	= altera_irq_domain_alloc,
	.free	= altera_irq_domain_free,
};

static int altera_allocate_domains(struct altera_msi *msi)
{
	struct irq_domain_info info = {
		.fwnode		= dev_fwnode(&msi->pdev->dev),
		.ops		= &msi_domain_ops,
		.host_data	= msi,
		.size		= msi->num_of_vectors,
	};

	msi->inner_domain = msi_create_parent_irq_domain(&info, &altera_msi_parent_ops);
	if (!msi->inner_domain) {
		dev_err(&msi->pdev->dev, "failed to create MSI domain\n");
		return -ENOMEM;
	}

	return 0;
}

static void altera_free_domains(struct altera_msi *msi)
{
	irq_domain_remove(msi->inner_domain);
}

static void altera_msi_remove(struct platform_device *pdev)
{
	struct altera_msi *msi = platform_get_drvdata(pdev);

	msi_writel(msi, 0, MSI_INTMASK);
	irq_set_chained_handler_and_data(msi->irq, NULL, NULL);

	altera_free_domains(msi);

	platform_set_drvdata(pdev, NULL);
}

static int altera_msi_probe(struct platform_device *pdev)
{
	struct altera_msi *msi;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	int ret;

	msi = devm_kzalloc(&pdev->dev, sizeof(struct altera_msi),
			   GFP_KERNEL);
	if (!msi)
		return -ENOMEM;

	mutex_init(&msi->lock);
	msi->pdev = pdev;

	msi->csr_base = devm_platform_ioremap_resource_byname(pdev, "csr");
	if (IS_ERR(msi->csr_base)) {
		dev_err(&pdev->dev, "failed to map csr memory\n");
		return PTR_ERR(msi->csr_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "vector_slave");
	msi->vector_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(msi->vector_base))
		return PTR_ERR(msi->vector_base);

	msi->vector_phy = res->start;

	if (of_property_read_u32(np, "num-vectors", &msi->num_of_vectors)) {
		dev_err(&pdev->dev, "failed to parse the number of vectors\n");
		return -EINVAL;
	}

	ret = altera_allocate_domains(msi);
	if (ret)
		return ret;

	msi->irq = platform_get_irq(pdev, 0);
	if (msi->irq < 0) {
		ret = msi->irq;
		goto err;
	}

	irq_set_chained_handler_and_data(msi->irq, altera_msi_isr, msi);
	platform_set_drvdata(pdev, msi);

	return 0;

err:
	altera_msi_remove(pdev);
	return ret;
}

static const struct of_device_id altera_msi_of_match[] = {
	{ .compatible = "altr,msi-1.0", NULL },
	{ },
};

static struct platform_driver altera_msi_driver = {
	.driver = {
		.name = "altera-msi",
		.of_match_table = altera_msi_of_match,
	},
	.probe = altera_msi_probe,
	.remove = altera_msi_remove,
};

static int __init altera_msi_init(void)
{
	return platform_driver_register(&altera_msi_driver);
}

static void __exit altera_msi_exit(void)
{
	platform_driver_unregister(&altera_msi_driver);
}

subsys_initcall(altera_msi_init);
MODULE_DEVICE_TABLE(of, altera_msi_of_match);
module_exit(altera_msi_exit);
MODULE_DESCRIPTION("Altera PCIe MSI support driver");
MODULE_LICENSE("GPL v2");
