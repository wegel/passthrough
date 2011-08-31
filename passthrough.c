#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>      /* kmalloc() */
#include <linux/fs.h>      /* everything... */
#include <linux/errno.h>   /* error codes */
#include <linux/timer.h>
#include <linux/types.h>   /* size_t */
#include <linux/fcntl.h>   /* O_ACCMODE */
#include <linux/hdreg.h>   /* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/bio.h>

MODULE_LICENSE("Dual BSD/GPL");

#define TARGET_NAME "/dev/sdd"


struct pt_dev {
	int major;
	struct request_queue *queue; 
	struct gendisk *gd;  
	struct block_device *target_dev;
};

static struct pt_dev *passthrough;

static int passthrough_make_request(struct request_queue *q, struct bio *bio)
{
	bio->bi_bdev = passthrough->target_dev;
	return 1;
}

int pt_getgeo(struct block_device * block_device, struct hd_geometry * hg)
{
	hg->heads = 255;
	hg->sectors = 63;

	hg->cylinders = get_capacity(block_device->bd_disk);
	sector_div(hg->cylinders, hg->heads * hg->sectors);

	return 0;
}

static struct block_device_operations pt_ops = {
	.owner           = THIS_MODULE,
	.getgeo = pt_getgeo
};


static int setup_passthrough_device(struct pt_dev *dev, const char *target_name)
{
	struct request_queue *q;

	dev->queue = blk_alloc_queue(GFP_KERNEL);
	if (dev->queue == NULL)
		return -1;

	blk_queue_make_request(dev->queue, passthrough_make_request);
	blk_queue_flush(dev->queue, REQ_FLUSH | REQ_FUA);

	dev->gd = alloc_disk(1);
	if (! dev->gd) {

		return -1;
	}

	dev->gd->major = passthrough->major;
	dev->gd->first_minor = 0;
	dev->gd->fops = &pt_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	dev->gd->flags |= GENHD_FL_EXT_DEVT;

	dev->target_dev = blkdev_get_by_path(target_name, FMODE_READ|FMODE_WRITE|FMODE_EXCL, dev);
	if(!dev->target_dev)
	{
		return -1;
	}

	if(!dev->target_dev->bd_disk)
	{
		return -1;
	}

	q = bdev_get_queue(dev->target_dev);
	if(!q)
	{
		return -1;
	}

	dev->gd->queue->limits.max_hw_sectors	= q->limits.max_hw_sectors;
	dev->gd->queue->limits.max_sectors	= q->limits.max_sectors;
	dev->gd->queue->limits.max_segment_size	= q->limits.max_segment_size;
	dev->gd->queue->limits.max_segments	= q->limits.max_segments;
	dev->gd->queue->limits.logical_block_size  = 512;
	dev->gd->queue->limits.physical_block_size = 512;
	set_bit(QUEUE_FLAG_NONROT, &dev->gd->queue->queue_flags);

	snprintf (dev->gd->disk_name, 32, "passthrough");

	set_capacity(dev->gd, get_capacity(dev->target_dev->bd_disk));

	add_disk(dev->gd);

	return 1;
}

static int __init pt_init(void)
{
	passthrough = kzalloc(sizeof(struct pt_dev), GFP_KERNEL);
	if(!passthrough)
	{
		return -ENOMEM;
	}

	passthrough->major = register_blkdev(0, "passthrough");
	if (passthrough->major <= 0) {
		return -EBUSY;
	}

	if(!setup_passthrough_device(passthrough, TARGET_NAME))
	{
		unregister_blkdev(passthrough->major, "passthrough");
		return -ENOMEM;
	}

	return 0;
}

static void pt_exit(void)
{
	if (passthrough->gd) {
		del_gendisk(passthrough->gd);
		put_disk(passthrough->gd);
	}
	if (passthrough->queue)
		blk_cleanup_queue(passthrough->queue);

	blkdev_put(passthrough->target_dev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);

	unregister_blkdev(passthrough->major, "passthrough");
	kfree(passthrough);
}

subsys_initcall(pt_init);
module_exit(pt_exit);