#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>


#define DRV_NAME "drv8139"
#define REALTEK_VENDER_ID 0x10ec
#define REALTEK_DEVICE_ID 0x8139

/*
 * Receive ring size
 * Warning: 64K ring has hardware issues and may lock up.
 */
#define RX_BUF_IDX 	2 /* 0==8K, 1==16K, 2==32K, 3==64K */
#define RX_BUF_LEN	(8192 << RX_BUF_IDX)
#define RX_BUF_PAD	16
#define RX_BUF_WRAP_PAD 2048 /* spare padding to handle lack of packet wrap */
#define RX_BUF_TOT_LEN  (RX_BUF_LEN + RX_BUF_PAD + RX_BUF_WRAP_PAD)

/* Number of Tx descriptor registers. */
#define NUM_TX_DESC	4

/* max supported ethernet frame size -- must be at least (dev->mtu+14+4).*/
#define MAX_ETH_FRAME_SIZE	1536

/* Size of the Tx bounce buffers -- must be at least (dev->mtu+14+4). */
#define TX_BUF_SIZE	MAX_ETH_FRAME_SIZE
#define TX_BUF_TOT_LEN	(TX_BUF_SIZE * NUM_TX_DESC)

#define assert(expr) \
	if(unlikely(!(expr))) {				\
	pr_err("Assertion failed! %s,%s,%s,line=%d\n",  \
	#expr, __FILE__, __func__, __LINE__);		\
	}


/* Symbolic offsets to registers. */
enum RTL8139_registers {
	TxStatus0	= 0x10,	/* Transmit status (Four 32bit registers). */
	TxAddr0		= 0x20,	/* Tx descriptors (also four 32bit). */
	RxBuf		= 0x30,
	ChipCmd 	= 0x37,
	RxBufPtr	= 0x38,
	IntrMask	= 0x3C,
	IntrStatus	= 0x3E,
	TxConfig	= 0x40,
	RxConfig	= 0x44,
	RxMissed	= 0x4C,	/* 24 bits valid, write clears. */
	MultiIntr	= 0x5C,
};

enum TxStatusBits {
	TxHostOwns	= 0x2000,
	TxUnderrun	= 0x4000,
	TxStatOK	= 0x8000,
	TxOutOfWindow	= 0x20000000,
	TxAborted	= 0x40000000,
	TxCarrierLost	= 0x80000000,
};

enum ChipCmdBits {
	CmdReset 	= 0x10,
	CmdRxEnb	= 0x08,
	CmdTxEnb	= 0x04,
	RxBufEmpty	= 0x01,
};

/* Interrupt register bits, using my own meaningful names. */
enum IntrStatusBits {
	PCIErr		= 0x8000,
	PCSTimeout	= 0x4000,
	RxFIFOOver	= 0x40,
	RxUnderrun	= 0x20,
	RxOverflow	= 0x10,
	TxErr		= 0x08,
	TxOK		= 0x04,
	RxErr		= 0x02,
	RxOK		= 0x01,

	RxAckBits	= RxFIFOOver | RxOverflow | RxOK,
};

/* write MMIO register, with flush */
/* Flush avoids drv8139 bug w/ posted MMIO writes */
#define RTL_W8_F(reg, val8)     do { iowrite8 ((val8), ioaddr + (reg)); ioread8 (ioaddr + (reg)); } while (0)
#define RTL_W16_F(reg, val16)   do { iowrite16 ((val16), ioaddr + (reg)); ioread16 (ioaddr + (reg)); } while (0)
#define RTL_W32_F(reg, val32)   do { iowrite32 ((val32), ioaddr + (reg)); ioread32 (ioaddr + (reg)); } while (0)

/* write MMIO register */
#define RTL_W8(reg, val8)       iowrite8 ((val8), ioaddr + (reg))
#define RTL_W16(reg, val16)     iowrite16 ((val16), ioaddr + (reg))
#define RTL_W32(reg, val32)     iowrite32 ((val32), ioaddr + (reg))

/* read MMIO register */
#define RTL_R8(reg)             ioread8 (ioaddr + (reg))
#define RTL_R16(reg)            ioread16 (ioaddr + (reg))
#define RTL_R32(reg)            ((unsigned long) ioread32 (ioaddr + (reg)))

struct pci_dev *pdev;

struct drv8139_private {
	struct pci_dev		*pci_dev;
	void __iomem		*mmio_addr;	/* memory mapped I/O addr */
	unsigned int 		regs_len;	/* length of I/O or MMI/O region */
	struct net_device 	*dev;

	unsigned char 		*rx_ring;
	unsigned int		cur_rx;		/* RX buf index of next pkt */
	dma_addr_t		rx_ring_dma;

	unsigned int 		tx_flag;
	unsigned int		cur_tx;
	unsigned int 		dirty_tx;
	unsigned char 		*tx_buf[NUM_TX_DESC];	/* Tx bounce buffers */
	unsigned char		*tx_bufs;		/* Tx bounce buffer region. */
	dma_addr_t		tx_bufs_dma;
};


static void __drv8139_cleanup_dev(struct net_device *dev)
{
	struct drv8139_private *tp = netdev_priv(dev);
	struct pci_dev *pdev;

	assert(dev != NULL);
	assert(tp->pci_dev != NULL);
	pdev = tp->pci_dev;

	if (tp->mmio_addr)
		pci_iounmap(pdev, tp->mmio_addr);

	/* it's ok to call this even if we have no regions to free */
	pci_release_regions(pdev);

	free_netdev(dev);
	pci_set_drvdata (pdev, NULL);
}

static void drv8139_chip_reset(void __iomem *ioaddr)
{
	int i;

	/* Soft reset the chip. */
	RTL_W8 (ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--) {
		barrier();
		if ((RTL_R8 (ChipCmd) & CmdReset) == 0)
			break;
		udelay (10);
	}
}

static struct net_device * drv8139_init_board(struct pci_dev *pdev)
{
	void __iomem *ioaddr;
	struct net_device *dev;
	struct drv8139_private *tp;
	unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;
	int rc, disable_dev_on_err = 0;

	assert(pdev != NULL);

	/* dev and priv zeroed in alloc_etherdev */
	dev = alloc_etherdev(sizeof (*tp));
	if (dev == NULL) {
		//TODO: dev_err(&pdev->dev, "Unable to alloc new net device\n");
		pr_info("Unable to alloc new net device\n");
		return ERR_PTR(-ENOMEM);
	}
	SET_NETDEV_DEV(dev, &pdev->dev);

	tp = netdev_priv(dev);
	tp->pci_dev = pdev;

	/* enable device (incl. PCI PM wakeup and hotplug setup) */
	rc = pci_enable_device (pdev);
	if (rc)
		goto err_out;

	/* get PCI memory mapped I/O space base addr from BAR1 */
	mmio_start = pci_resource_start (pdev, 1);
	mmio_end = pci_resource_end (pdev, 1);
	mmio_flags = pci_resource_flags (pdev, 1);
	mmio_len = pci_resource_len (pdev, 1);

	/* set this immediately, we need to know before
 	 * we talk to the chip directly */
	pr_debug("MMIO region size == 0x%02lX\n", mmio_len);

	/* make sure above region is MMIO */
	if (!(mmio_flags & IORESOURCE_MEM)) {
		pr_info("region #1 not an MMIO resource, aborting\n");
		rc = -ENODEV;
		goto err_out;
	}

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;
	disable_dev_on_err = 1;

	/* enable PCI bus-mastering */
	pci_set_master(pdev);

	/* ioremap MMIO region */
	ioaddr = pci_iomap(pdev, 1, 0);
	if (ioaddr == NULL) {
		pr_info("cannot remap MMIO, trying PIO\n");
		pci_release_regions(pdev);
		rc = -EIO;
		goto err_out;
	}
	dev->base_addr = (long) ioaddr;
	tp->regs_len = mmio_len;
	tp->mmio_addr = ioaddr;

	drv8139_chip_reset(ioaddr);

	return dev;

err_out:
	__drv8139_cleanup_dev(dev);
	if (disable_dev_on_err)
		pci_disable_device(pdev);
	return ERR_PTR(rc);
}

static irqreturn_t drv8139_interrupt(int irq, void *dev_instance)
{
	return 0;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void drv8139_init_ring(struct net_device *dev)
{
	struct drv8139_private *tp = netdev_priv(dev);
	int i;

	tp->cur_rx = 0;
	tp->cur_tx = 0;
	tp->dirty_tx = 0;

	for (i = 0; i < NUM_TX_DESC; i++)
		tp->tx_buf[i] = &tp->tx_bufs[i * TX_BUF_SIZE];
}

/* Start the hardware at open or resume. */
static void drv8139_hw_start(struct net_device *dev)
{
	struct drv8139_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

#if 0
	drv8139_chip_reset (ioaddr);

	/* init Rx ring buffer DMA address */
	RTL_W32_F (RxBuf, tp->rx_ring_dma);

	/* init Tx buffer DMA addresses */
	for (i = 0; i < NUM_TX_DESC; i++)
		RTL_W32_F (TxAddr0 + (i * 4), tp->tx_bufs_dma + (tp->tx_buf[i] - tp->tx_bufs));

	/* Must enable Tx/Rx before setting transfer thresholds! */
	RTL_W8 (ChipCmd, CmdRxEnb | CmdTxEnb);

	RTL_W32 (RxConfig, 0x0000178e); 
	RTL_W32 (TxConfig, 0x00000600); /* DMA burst size 1024 */
#endif	
}


/* net_device_ops */
static int drv8139_open(struct net_device *dev)
{
	struct drv8139_private *tp = netdev_priv(dev);
	int retval;

	retval = request_irq (dev->irq, drv8139_interrupt, IRQF_SHARED, dev->name, dev);
	if (retval)
		return retval;

	tp->tx_bufs = dma_alloc_coherent(&tp->pci_dev->dev, TX_BUF_TOT_LEN,
						&tp->tx_bufs_dma, GFP_KERNEL);
	tp->rx_ring = dma_alloc_coherent(&tp->pci_dev->dev, RX_BUF_TOT_LEN,
						&tp->rx_ring_dma, GFP_KERNEL);
	if (tp->tx_bufs == NULL || tp->rx_ring == NULL) {
		free_irq(dev->irq, dev);

		if (tp->tx_bufs)
			dma_free_coherent(&tp->pci_dev->dev, TX_BUF_TOT_LEN,
						tp->tx_bufs, tp->tx_bufs_dma);
		if (tp->rx_ring)
			dma_free_coherent(&tp->pci_dev->dev, RX_BUF_TOT_LEN,
						tp->rx_ring, tp->rx_ring_dma);

		return -ENOMEM;
	}

	tp->tx_flag = 0;
	drv8139_init_ring (dev);
	drv8139_hw_start (dev);
	netif_start_queue (dev);

	return 0;
}

static int drv8139_close(struct net_device *dev)
{
	return 0;
}

static struct net_device_stats *drv8139_get_stats(struct net_device *dev)
{
	return NULL;
}

static int drv8139_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	return 0;
}

static const struct net_device_ops drv8139_netdev_ops = {
	.ndo_open		= drv8139_open,
	.ndo_stop		= drv8139_close,
	.ndo_get_stats		= drv8139_get_stats,
	.ndo_start_xmit		= drv8139_start_xmit,
};


static int __init drv8139_init_module(void)
{
	struct pci_dev *pdev;
	struct net_device *dev;
	struct drv8139_private *tp;
	void __iomem *ioaddr;
	int i, rc;

	if (no_pci_devices()) {
		pr_info("PCI is not present\n");
		return -ENODEV;
	}
	
	pdev = pci_get_device(REALTEK_VENDER_ID, REALTEK_DEVICE_ID, NULL);
	if (pdev == NULL) {
		pr_info("Device not found\n");
		return -ENODEV;
	}

	dev = drv8139_init_board(pdev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	
	tp = netdev_priv(dev);
	tp->dev = dev;

	ioaddr = tp->mmio_addr;
	assert (ioaddr != NULL);

	dev->addr_len = 6;
	for (i = 0; i < 6; i++) {
		dev->dev_addr[i] = ioread8(ioaddr+i);
		dev->broadcast[i] = 0xff;
	}
	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

	dev->netdev_ops = &drv8139_netdev_ops;
	dev->irq = pdev->irq; /* interrupt number */

	rc = register_netdev(dev);
	if (rc) {
		pr_info("Could not register netdevice\n");
		__drv8139_cleanup_dev(dev);
		pci_disable_device(pdev);
		return rc;
	}

	pci_set_drvdata (pdev, dev);

	return 0;
}

static void __exit drv8139_cleanup_module(void)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	assert(dev != NULL);
	unregister_netdev(dev);
	__drv8139_cleanup_dev(dev);
	pci_disable_device(pdev);
}

module_init(drv8139_init_module);
module_exit(drv8139_cleanup_module);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evans Lai <evanslai@gmail.com>");
