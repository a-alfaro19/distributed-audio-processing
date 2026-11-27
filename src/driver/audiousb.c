/*
 * audiousb.c - Driver USB a la medida para el visualizador 8x8 (Raspberry Pi Pico)
 *
 * Corre en el nodo maestro (Linux). La Pico expone una interfaz USB
 * vendor-specific (clase 0xFF) con endpoints bulk, asi que ningun driver
 * del sistema (cdc_acm, etc.) la reclama. Este modulo se enlaza a ella y
 * publica /dev/audiousbN.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>

#include "audiousb_ioctl.h"

/* Deben coincidir con el descriptor USB del firmware de la Pico */
#define VENDOR_ID        0x2E8A
#define PRODUCT_ID       0x4001

#define DEVICE_NAME      "audiousb"
#define MINOR_BASE       192
#define USB_TIMEOUT_MS   1000
/* FRAME y PATTERN son los comandos mas grandes (1 + 8 bytes) */
#define MAX_PKT_SIZE     (1 + AUDIOUSB_FRAME_SIZE)
_Static_assert(AUDIOUSB_PATTERN_SIZE <= AUDIOUSB_FRAME_SIZE,
	       "MAX_PKT_SIZE debe cubrir el comando PATTERN");

struct audiousb {
	struct usb_device    *udev;
	struct usb_interface *interface;   /* NULL tras la desconexion */
	struct mutex          io_mutex;
	struct kref           kref;
	u8                   *out_buf;     /* MAX_PKT_SIZE */
	u8                   *in_buf;      /* in_size (maxpacket) */
	size_t                in_size;
	u8                    out_ep;
	u8                    in_ep;       /* 0 si no hay endpoint IN */
};

static struct usb_driver audiousb_driver;

static const struct usb_device_id audiousb_table[] = {
	{ USB_DEVICE_INTERFACE_CLASS(VENDOR_ID, PRODUCT_ID, USB_CLASS_VENDOR_SPEC) },
	{ }
};
MODULE_DEVICE_TABLE(usb, audiousb_table);

/* Se llama cuando se suelta la ultima referencia (kref_put) */
static void audiousb_delete(struct kref *kref)
{
	struct audiousb *dev = container_of(kref, struct audiousb, kref);

	usb_put_dev(dev->udev);
	kfree(dev->in_buf);
	kfree(dev->out_buf);
	kfree(dev);
}

/*
 * Envia un paquete a la Pico y espera a que termine. Toma io_mutex.
 * 'pkt' puede estar en el stack: se copia a out_buf (DMA-safe).
 */
static int audiousb_xfer(struct audiousb *dev, const u8 *pkt, int len)
{
	int actual = 0;
	int ret;

	if (mutex_lock_interruptible(&dev->io_mutex))
		return -ERESTARTSYS;

	if (!dev->interface) {              /* la Pico fue desconectada */
		ret = -ENODEV;
		goto out;
	}

	memcpy(dev->out_buf, pkt, len);
	ret = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, dev->out_ep),
			   dev->out_buf, len, &actual, USB_TIMEOUT_MS);
	if (ret) {
		dev_err(&dev->interface->dev, "error en bulk OUT (%d)\n", ret);
		goto out;
	}
	if (actual != len)
		ret = -EIO;
out:
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static int audiousb_open(struct inode *inode, struct file *file)
{
	struct usb_interface *interface;
	struct audiousb *dev;

	interface = usb_find_interface(&audiousb_driver, iminor(inode));
	if (!interface)
		return -ENODEV;

	dev = usb_get_intfdata(interface);
	if (!dev)
		return -ENODEV;

	kref_get(&dev->kref);               /* una referencia por archivo abierto */
	file->private_data = dev;
	return 0;
}

static int audiousb_release(struct inode *inode, struct file *file)
{
	struct audiousb *dev = file->private_data;

	if (dev)
		kref_put(&dev->kref, audiousb_delete);
	return 0;
}

/* write(): exactamente AUDIOUSB_FRAME_SIZE bytes = un frame (util para pruebas con echo) */
static ssize_t audiousb_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	struct audiousb *dev = file->private_data;
	u8 pkt[MAX_PKT_SIZE];
	int i, ret;

	if (count != AUDIOUSB_FRAME_SIZE)
		return -EINVAL;

	if (copy_from_user(&pkt[1], ubuf, AUDIOUSB_FRAME_SIZE))
		return -EFAULT;

	for (i = 1; i <= AUDIOUSB_FRAME_SIZE; i++)
		if (pkt[i] > AUDIOUSB_ROWS)
			return -EINVAL;

	pkt[0] = AUDIOUSB_CMD_FRAME;
	ret = audiousb_xfer(dev, pkt, sizeof(pkt));
	return ret ? ret : (ssize_t)count;
}

/* read(): lee un paquete de estado/ACK que envie la Pico (opcional) */
static ssize_t audiousb_read(struct file *file, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	struct audiousb *dev = file->private_data;
	int actual = 0;
	int ret;

	if (!count)
		return 0;

	if (mutex_lock_interruptible(&dev->io_mutex))
		return -ERESTARTSYS;

	if (!dev->interface) {
		ret = -ENODEV;
		goto out;
	}
	if (!dev->in_ep) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	/* Se pide siempre in_size (>= maxpacket) para evitar -EOVERFLOW */
	ret = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, dev->in_ep),
			   dev->in_buf, dev->in_size, &actual, USB_TIMEOUT_MS);
	if (ret) {
		dev_err(&dev->interface->dev, "error en bulk IN (%d)\n", ret);
		goto out;
	}

	actual = min_t(size_t, actual, count);
	if (copy_to_user(ubuf, dev->in_buf, actual))
		ret = -EFAULT;
	else
		ret = actual;
out:
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static long audiousb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct audiousb *dev = file->private_data;
	void __user *argp = (void __user *)arg;
	u8 pkt[MAX_PKT_SIZE];
	int len, i;

	if (_IOC_TYPE(cmd) != AUDIOUSB_IOC_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case AUDIOUSB_IOC_SET_FRAME: {
		struct audiousb_frame f;

		if (copy_from_user(&f, argp, sizeof(f)))
			return -EFAULT;
		for (i = 0; i < AUDIOUSB_COLS; i++)
			if (f.levels[i] > AUDIOUSB_ROWS)
				return -EINVAL;
		pkt[0] = AUDIOUSB_CMD_FRAME;
		memcpy(&pkt[1], f.levels, AUDIOUSB_FRAME_SIZE);
		len = 1 + AUDIOUSB_FRAME_SIZE;
		break;
	}
	case AUDIOUSB_IOC_SET_POINT: {
		struct audiousb_point p;

		if (copy_from_user(&p, argp, sizeof(p)))
			return -EFAULT;
		if (p.x >= AUDIOUSB_COLS || p.y >= AUDIOUSB_ROWS || p.on > 1)
			return -EINVAL;
		pkt[0] = AUDIOUSB_CMD_POINT;
		pkt[1] = p.x;
		pkt[2] = p.y;
		pkt[3] = p.on;
		len = 4;
		break;
	}
	case AUDIOUSB_IOC_SET_PATTERN: {
		struct audiousb_pattern p;

		if (copy_from_user(&p, argp, sizeof(p)))
			return -EFAULT;
		pkt[0] = AUDIOUSB_CMD_PATTERN;
		memcpy(&pkt[1], p.rows, AUDIOUSB_PATTERN_SIZE);
		len = 1 + AUDIOUSB_PATTERN_SIZE;
		break;
	}
	case AUDIOUSB_IOC_CLEAR:
		pkt[0] = AUDIOUSB_CMD_CLEAR;
		len = 1;
		break;
	case AUDIOUSB_IOC_SET_TIMING: {
		struct audiousb_timing t;

		if (copy_from_user(&t, argp, sizeof(t)))
			return -EFAULT;
		if (t.refresh_ms < AUDIOUSB_REFRESH_MIN_MS ||
		    t.refresh_ms > AUDIOUSB_REFRESH_MAX_MS)
			return -EINVAL;
		pkt[0] = AUDIOUSB_CMD_TIMING;
		pkt[1] = t.refresh_ms & 0xFF;
		pkt[2] = t.refresh_ms >> 8;
		pkt[3] = t.decay;
		len = 4;
		break;
	}
	default:
		return -ENOTTY;
	}

	return audiousb_xfer(dev, pkt, len);
}

static const struct file_operations audiousb_fops = {
	.owner          = THIS_MODULE,
	.open           = audiousb_open,
	.release        = audiousb_release,
	.read           = audiousb_read,
	.write          = audiousb_write,
	.unlocked_ioctl = audiousb_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
	.llseek         = noop_llseek,
};

static struct usb_class_driver audiousb_class = {
	.name       = DEVICE_NAME "%d",
	.fops       = &audiousb_fops,
	.minor_base = MINOR_BASE,
};

static int audiousb_probe(struct usb_interface *interface,
			  const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *bulk_in = NULL, *bulk_out = NULL;
	struct audiousb *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	kref_init(&dev->kref);
	mutex_init(&dev->io_mutex);
	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* El endpoint OUT es obligatorio; el IN es opcional */
	ret = usb_find_bulk_out_endpoint(interface->cur_altsetting, &bulk_out);
	if (ret) {
		dev_err(&interface->dev, "no se encontro endpoint bulk OUT\n");
		goto error;
	}
	dev->out_ep = bulk_out->bEndpointAddress;

	dev->out_buf = kmalloc(MAX_PKT_SIZE, GFP_KERNEL);
	if (!dev->out_buf) {
		ret = -ENOMEM;
		goto error;
	}

	if (!usb_find_bulk_in_endpoint(interface->cur_altsetting, &bulk_in)) {
		dev->in_ep = bulk_in->bEndpointAddress;
		dev->in_size = usb_endpoint_maxp(bulk_in);
		dev->in_buf = kmalloc(dev->in_size, GFP_KERNEL);
		if (!dev->in_buf) {
			ret = -ENOMEM;
			goto error;
		}
	}

	/* intfdata antes de registrar: open() ya puede llegar tras el register */
	usb_set_intfdata(interface, dev);

	ret = usb_register_dev(interface, &audiousb_class);
	if (ret) {
		dev_err(&interface->dev, "no se pudo registrar el dispositivo (%d)\n", ret);
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/*
	 * Con CONFIG_USB_DYNAMIC_MINORS (Ubuntu, Debian...) el kernel ignora
	 * MINOR_BASE y el numero depende de otros dispositivos usbmisc
	 * (hiddev...). Se imprime el nombre real que le asigno.
	 */
	dev_info(&interface->dev, "Pico conectada: /dev/%s (out=0x%02x in=0x%02x)\n",
		 dev_name(interface->usb_dev), dev->out_ep, dev->in_ep);
	return 0;

error:
	kref_put(&dev->kref, audiousb_delete);   /* libera buffers y usb_put_dev */
	return ret;
}

static void audiousb_disconnect(struct usb_interface *interface)
{
	struct audiousb *dev = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);
	usb_deregister_dev(interface, &audiousb_class);

	/* Espera a que termine una transferencia en curso y marca la desconexion */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	kref_put(&dev->kref, audiousb_delete);
	dev_info(&interface->dev, "Pico desconectada\n");
}

static struct usb_driver audiousb_driver = {
	.name       = DEVICE_NAME,
	.id_table   = audiousb_table,
	.probe      = audiousb_probe,
	.disconnect = audiousb_disconnect,
};

module_usb_driver(audiousb_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver USB a la medida - visualizador de audio 8x8 con Raspberry Pi Pico (CE 4303)");