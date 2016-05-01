#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/aio.h>
#include <linux/sec_debug.h>
//#include <plat/map-base.h>
#include <asm/map.h>
#include <asm/tlbflush.h>
////#include <mach/regs-pmu.h>
//#include <mach/regs-clock.h>
#ifdef CONFIG_NO_BOOTMEM
#include <linux/memblock.h>
#endif

/*
 * Example usage: sec_log=256K@0x45000000
 * In above case, log_buf size is 256KB and its base address is
 * 0x45000000 physically. Actually, *(int *)(base - 8) is log_magic and
 * *(int *)(base - 4) is log_ptr. So we reserve (size + 8) bytes from
 * (base - 8).
 */
#define LOG_MAGIC 0x4d474f4c	/* "LOGM" */

#ifdef CONFIG_SEC_AVC_LOG
static unsigned *sec_avc_log_ptr;
static char *sec_avc_log_buf;
static unsigned sec_avc_log_size;
#if 0 /* ZERO WARNING */
static struct map_desc avc_log_buf_iodesc[] __initdata = {
	{
		.virtual = (unsigned long)S3C_VA_AUXLOG_BUF,
		.type = MT_DEVICE
	}
};
#endif

static int __init sec_avc_log_setup(char *str)
{
	unsigned size = memparse(str, &str);
	unsigned long base = 0;
	unsigned *sec_avc_log_mag;
	/* If we encounter any problem parsing str ... */
	if (!size || size != roundup_pow_of_two(size) || *str != '@'
		|| kstrtoul(str + 1, 0, &base))
			goto out;

#ifdef CONFIG_NO_BOOTMEM
	if (memblock_is_region_reserved(base - 8, size + 8) ||
			memblock_reserve(base - 8, size + 8)) {
#else
	if (reserve_bootmem(base - 8 , size + 8, BOOTMEM_EXCLUSIVE)) {
#endif
			pr_err("%s: failed reserving size %d " \
						"at base 0x%lx\n", __func__, size, base);
			goto out;
	}
	/* TODO: remap noncached area.
	avc_log_buf_iodesc[0].pfn = __phys_to_pfn((unsigned long)base);
	avc_log_buf_iodesc[0].length = (unsigned long)(size);
	iotable_init(avc_log_buf_iodesc, ARRAY_SIZE(avc_log_buf_iodesc));
	sec_avc_log_mag = S3C_VA_KLOG_BUF - 8;
	sec_avc_log_ptr = S3C_VA_AUXLOG_BUF - 4;
	sec_avc_log_buf = S3C_VA_AUXLOG_BUF;
	*/
	sec_avc_log_mag = phys_to_virt(base) - 8;
	sec_avc_log_ptr = phys_to_virt(base) - 4;
	sec_avc_log_buf = phys_to_virt(base);
	sec_avc_log_size = size;

	pr_info("%s: *sec_avc_log_ptr:%x " \
		"sec_avc_log_buf:%p sec_log_size:0x%x\n",
		__func__, *sec_avc_log_ptr, sec_avc_log_buf,
		sec_avc_log_size);

	if (*sec_avc_log_mag != LOG_MAGIC) {
		pr_info("%s: no old log found\n", __func__);
		*sec_avc_log_ptr = 0;
		*sec_avc_log_mag = LOG_MAGIC;
	}
	return 1;
out:
	return 0;
}
__setup("sec_avc_log=", sec_avc_log_setup);

#define BUF_SIZE 512
void sec_debug_avc_log(char *fmt, ...)
{
	va_list args;
	char buf[BUF_SIZE];
	int len = 0;
	unsigned long idx;
	unsigned long size;

	/* In case of sec_avc_log_setup is failed */
	if (!sec_avc_log_size)
		return;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	idx = *sec_avc_log_ptr;
	size = strlen(buf);

	if (idx + size > sec_avc_log_size - 1) {
		len = scnprintf(&sec_avc_log_buf[0], size + 1, "%s\n", buf);
		*sec_avc_log_ptr = len;
	} else {
		len = scnprintf(&sec_avc_log_buf[idx], size + 1, "%s\n", buf);
		*sec_avc_log_ptr += len;
	}
}
EXPORT_SYMBOL(sec_debug_avc_log);

static ssize_t sec_avc_log_write(struct file *file,
		const char __user *buf,
		size_t count, loff_t *ppos)

{
	char *page = NULL;
	ssize_t ret;
	int new_value;

	if (!sec_avc_log_buf)
		return 0;

	ret = -ENOMEM;
	if (count >= PAGE_SIZE)
		return ret;

	ret = -ENOMEM;
	page = (char *)get_zeroed_page(GFP_KERNEL);
	if (!page)
		return ret;;

	ret = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out;

	ret = -EINVAL;
	if (sscanf(page, "%u", &new_value) != 1) {
		pr_info("%s\n", page);
		/* print avc_log to sec_avc_log_buf */
		sec_debug_avc_log("%s", page);
	} 
	ret = count;
out:
	free_page((unsigned long)page);
	return ret;
}

static ssize_t sec_avc_log_read(struct file *file, char __user *buf,
		size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (sec_avc_log_buf == NULL)
		return 0;

	if (pos >= *sec_avc_log_ptr)
		return 0;

	count = min(len, (size_t)(*sec_avc_log_ptr - pos));
	if (copy_to_user(buf, sec_avc_log_buf + pos, count))
		return -EFAULT;
	*offset += count;
	return count;
}

static const struct file_operations avc_msg_file_ops = {
	.owner = THIS_MODULE,
	.read = sec_avc_log_read,
	.write = sec_avc_log_write,
	.llseek = generic_file_llseek,
};

static int __init sec_avc_log_late_init(void)
{
	struct proc_dir_entry *entry;
	if (sec_avc_log_buf == NULL)
		return 0;

	entry = proc_create("avc_msg", S_IFREG | S_IRUGO, NULL, 
			&avc_msg_file_ops);
	if (!entry) {
		pr_err("%s: failed to create proc entry\n", __func__);
		return 0;
	}

	proc_set_size(entry, sec_avc_log_size);
	return 0;
}

late_initcall(sec_avc_log_late_init);

#endif /* CONFIG_SEC_AVC_LOG */


#ifdef CONFIG_SEC_KSHOT_LOG

#define S5P_VA_SS_BASE			((void __iomem __force *)(VMALLOC_START + 0xF6000000))
#define S5P_VA_SS_LAST_LOGBUF	(S5P_VA_SS_BASE + 0x200)
#define S5P_VA_KS_LOGBUF		(S5P_VA_SS_BASE + 0x800000)
#define S5P_VA_LK_LOGBUF		(S5P_VA_SS_BASE + 0x400000)

extern void register_hook_logbuf1(void (*)(const char *, size_t));

struct exynos_ss_base {
	size_t size;
	size_t vaddr;
	size_t paddr;
	unsigned int persist;
	unsigned int enabled;
};

struct exynos_ss_item {
	char *name;
	struct exynos_ss_base entry;
	unsigned char *head_ptr;
	unsigned char *curr_ptr;
	unsigned long long time;
};
struct exynos_ss_item itemk;

static unsigned *sec_log_idx_ptr; /* Log buffer index pointer*/

static char *last_kmsg_buffer;
static unsigned last_kmsg_size;

static int __init sec_log_save_old(void)
{
	struct exynos_ss_item *item = &itemk;
#if 0
	last_kmsg_size =
	    min((size_t)item->entry.size, (size_t)*sec_log_idx_ptr-(size_t)item->entry.paddr);
#endif
	    
	if (last_kmsg_size && last_kmsg_buffer) {
#if 0 
		for (i = 0; i < last_kmsg_size; i++)
			last_kmsg_buffer[i] =
			    item->head_ptr[(*sec_log_idx_ptr - last_kmsg_size +
					 i) & (item->entry.size - 1)];
#else
		memcpy((char*)last_kmsg_buffer, (char*)item->head_ptr, (size_t)last_kmsg_size);
#endif
		return 1;
	} else {
		return 0;
	}
}
static inline int exynos_ss_check_eob(struct exynos_ss_item *item,
						size_t size)
{
	size_t max, cur;

	max = (size_t)(item->head_ptr + item->entry.size);
	cur = (size_t)(item->curr_ptr + size);

	if (unlikely(cur > max))
		return -1;
	else
		return 0;
}

void exynos_ks_hook_logbuf(const char *buf, size_t size)
{
	size_t last_buf, align;
	struct exynos_ss_item *item = &itemk;

	if (exynos_ss_check_eob(item, size)) {
		item->curr_ptr = item->head_ptr;
	}

	memcpy(item->curr_ptr, buf, size);
	item->curr_ptr += size;

	/*it used for making last_kmsg size, when it's reach to the full buffer size*/ 
	#if 0
	(*sec_log_idx_ptr)+=size;
	#endif

	/*  save the address of last_buf to physical address */
	align = (size_t)(item->entry.size - 1);
	last_buf = (size_t)item->curr_ptr;
	__raw_writel((last_buf & align) | (item->entry.paddr & ~align), S5P_VA_SS_LAST_LOGBUF);
}

static int __init sec_kshot_log_setup(char *str)
{
	//	unsigned size = memparse(str, &str);
	struct map_desc ess_iodesc[3];
	unsigned long base = 0;
	size_t last_buf, align;
	size_t vaddr, paddr, size;
	int bOk = 0;

	if(sec_debug_get_debug_level() != 0)
		return 0;

	if (kstrtoul(str, 0, (unsigned long *)&base))
		goto out;

	if (memblock_is_region_reserved(base+0x800000, 0x200000) ||
			memblock_reserve(base +0x800000, 0x200000)) {
			printk(KERN_ERR"kshot log reserving err :%lx \n",base+0x800000);
			goto out;
	}
	if (memblock_is_region_reserved(base+0x400000, 0x200000) ||
			memblock_reserve(base +0x400000, 0x200000)) {
			printk(KERN_ERR"last_kmsg log reservng error :%lx \n",base+0x800000);
			goto out;
	}
	if (memblock_is_region_reserved(base + 0x200,  0x1000) ||
			memblock_reserve(base +0x200, 0x1000)) {
			printk(KERN_ERR"kshot header reserving err :%lx \n",base+0x200);
			goto out;
	}

	itemk.entry.vaddr = (size_t)S5P_VA_KS_LOGBUF; 	
	itemk.entry.paddr = (base + 0x800000); 	
	itemk.entry.size = 0x200000;
	itemk.head_ptr = (unsigned char*)itemk.entry.vaddr;
	itemk.curr_ptr = (unsigned char*)itemk.entry.vaddr;

	/*  =>offset definition
	 *  #define _KLOG_MAGIC_PTR     (unsigned int *)(_log_buf + 0x200 + 0x4);
	 *  #define _KLOG_PTR           (unsigned int *)(_log_buf + 0x200);
	 *  #define _KLOG_BUF           ((char *)(_log_buf + CONFIG_ESS_OFF_KERNEL)) 
	 *  #define _LAST_KMSG_BUF      ((char *)(_log_buf + 0x400000)) 
	 */

	/*bootloer/kernel kernel log buffer*/
	ess_iodesc[0].type = MT_NORMAL_NC;
	ess_iodesc[0].length = itemk.entry.size;
	ess_iodesc[0].virtual = itemk.entry.vaddr;
	ess_iodesc[0].pfn = __phys_to_pfn(itemk.entry.paddr);

	/*header area*/
	ess_iodesc[1].type = MT_NORMAL_NC;
	ess_iodesc[1].length = 0x200000;
	ess_iodesc[1].virtual = (size_t)S5P_VA_SS_BASE;
	ess_iodesc[1].pfn = __phys_to_pfn(base);

	/*last_kmsg buffer area*/
	ess_iodesc[2].type = MT_NORMAL_NC;
	ess_iodesc[2].length = 0x200000;
	ess_iodesc[2].virtual = (size_t)S5P_VA_LK_LOGBUF;
	ess_iodesc[2].pfn = __phys_to_pfn(base+0x400000);

	iotable_init(ess_iodesc,3);

    /*ss_fixmap*/
	paddr = itemk.entry.paddr;
	vaddr = itemk.entry.vaddr;
	size  = itemk.entry.size;
	/*load last_buf address value(phy) by virt address*/
	last_buf = (size_t)__raw_readl(S5P_VA_SS_LAST_LOGBUF);
	align = (size_t)(size - 1);

	/*last_kmsg reserved buffer*/
	last_kmsg_buffer = S5P_VA_LK_LOGBUF;
	last_kmsg_size = itemk.entry.size;
	sec_log_idx_ptr = S5P_VA_SS_LAST_LOGBUF; 

	/*check physical address offset of kernel logbuf*/
	if ((size_t)(last_buf & ~align) == (size_t)(paddr & ~align)) {
		/*assumed valid address, conversion to virt*/
		itemk.curr_ptr = (unsigned char *)
			((last_buf & align) |
			 (size_t)(vaddr & ~align));
	} else {
		/*invalid address, set to first line*/
		itemk.curr_ptr = (unsigned char *)vaddr;
		/*initialize logbuf to 0*/
		memset((size_t *)vaddr, 0, size);
	}

	bOk = sec_log_save_old();

	if (bOk) {
		pr_info("%s: saved old log at %d@%p\n",
			__func__, last_kmsg_size, last_kmsg_buffer);
	} else {
		pr_err("%s: failed saving old log \n",__func__);
	}

	register_hook_logbuf1(exynos_ks_hook_logbuf);

	return 1;
out:
	return 0;
}
__setup("sec_kshot_log=", sec_kshot_log_setup);

static ssize_t sec_kshot_log_read(struct file *file, char __user *buf,
		size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count = 0;
	size_t log_size = last_kmsg_size;
	const char *log = last_kmsg_buffer;

	if(sec_debug_get_debug_level() != 0)
		return 0;

	if (pos < log_size) {
		count = min(len, (size_t)(log_size - pos));
		if (copy_to_user(buf, log + pos, count))
			return -EFAULT;
	}

	*offset += count;
	return count;
}

static const struct file_operations kshot_msg_file_ops = {
	.owner = THIS_MODULE,
	.read = sec_kshot_log_read,
	.llseek = generic_file_llseek,
};

static int __init sec_kshot_log_late_init(void)
{
	struct proc_dir_entry *entry;
	struct exynos_ss_item *item = &itemk;

	if(sec_debug_get_debug_level() != 0)
		return 0;

	if (!item->head_ptr)
		return 0;

	entry = proc_create("last_kmsg", S_IFREG | S_IRUGO, NULL, 
			&kshot_msg_file_ops);

	if (!entry) {
		pr_err("%s: failed to create proc entry\n", __func__);
		return 0;
	}
	/* proc size set*/
	//proc_set_size(entry, item->entry.size);
	proc_set_size(entry, last_kmsg_size);
	return 0;
}

late_initcall(sec_kshot_log_late_init);

#endif ///CONFIG_SEC_KSHOT_LOG

#ifdef CONFIG_SEC_DEBUG_TSP_LOG
static unsigned *sec_tsp_log_ptr;
static char *sec_tsp_log_buf;
static unsigned sec_tsp_log_size;

static int __init sec_tsp_log_setup(char *str)
{
	unsigned size = memparse(str, &str);
	unsigned long base = 0;
	unsigned *sec_tsp_log_mag;
	/* If we encounter any problem parsing str ... */
	if (!size || size != roundup_pow_of_two(size) || *str != '@'
		|| kstrtoul(str + 1, 0, &base))
			goto out;

#ifdef CONFIG_NO_BOOTMEM
	if (memblock_is_region_reserved(base - 8, size + 8) ||
			memblock_reserve(base - 8, size + 8)) {
#else
	if (reserve_bootmem(base - 8 , size + 8, BOOTMEM_EXCLUSIVE)) {
#endif
			pr_err("%s: failed reserving size %d " \
						"at base 0x%lx\n", __func__, size, base);
			goto out;
	}

	sec_tsp_log_mag = phys_to_virt(base) - 8;
	sec_tsp_log_ptr = phys_to_virt(base) - 4;
	sec_tsp_log_buf = phys_to_virt(base);
	sec_tsp_log_size = size;

	pr_info("%s: *sec_tsp_log_ptr:%x " \
		"sec_tsp_log_buf:%p sec_tsp_log_size:0x%x\n",
		__func__, *sec_tsp_log_ptr, sec_tsp_log_buf,
		sec_tsp_log_size);

	if (*sec_tsp_log_mag != LOG_MAGIC) {
		pr_info("%s: no old log found\n", __func__);
		*sec_tsp_log_ptr = 0;
		*sec_tsp_log_mag = LOG_MAGIC;
	}
	return 1;
out:
	return 0;
}
__setup("sec_tsp_log=", sec_tsp_log_setup);

static int sec_tsp_log_timestamp(unsigned int idx)
{
	/* Add the current time stamp */
	char tbuf[50];
	unsigned tlen;
	unsigned long long t;
	unsigned long nanosec_rem;

	t = local_clock();
	nanosec_rem = do_div(t, 1000000000);
	tlen = sprintf(tbuf, "[%5lu.%06lu] ",
			(unsigned long) t,
			nanosec_rem / 1000);

	/* Overflow buffer size */
	if (idx + tlen > sec_tsp_log_size - 1) {
		tlen = scnprintf(&sec_tsp_log_buf[0],
						tlen + 1, "%s", tbuf);
		*sec_tsp_log_ptr = tlen;
	} else {
		tlen = scnprintf(&sec_tsp_log_buf[idx], tlen + 1, "%s", tbuf);
		*sec_tsp_log_ptr += tlen;
	}

	return *sec_tsp_log_ptr;
}

#define TSP_BUF_SIZE 512
void sec_debug_tsp_log(char *fmt, ...)
{
	va_list args;
	char buf[TSP_BUF_SIZE];
	int len = 0;
	unsigned int idx;
	unsigned int size;

	/* In case of sec_tsp_log_setup is failed */
	if (!sec_tsp_log_size)
		return;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	idx = *sec_tsp_log_ptr;
	size = strlen(buf);

	idx = sec_tsp_log_timestamp(idx);

	/* Overflow buffer size */
	if (idx + size > sec_tsp_log_size - 1) {
		len = scnprintf(&sec_tsp_log_buf[0],
						size + 1, "%s", buf);
		*sec_tsp_log_ptr = len;
	} else {
		len = scnprintf(&sec_tsp_log_buf[idx], size + 1, "%s", buf);
		*sec_tsp_log_ptr += len;
	}
}
EXPORT_SYMBOL(sec_debug_tsp_log);

void sec_debug_tsp_log_msg(char *msg, char *fmt, ...)
{
	va_list args;
	char buf[TSP_BUF_SIZE];
	int len = 0;
	unsigned int idx;
	unsigned int size;
	unsigned int size_dev_name;

	/* In case of sec_tsp_log_setup is failed */
	if (!sec_tsp_log_size)
		return;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	idx = *sec_tsp_log_ptr;
	size = strlen(buf);
	size_dev_name = strlen(msg);

	idx = sec_tsp_log_timestamp(idx);

	/* Overflow buffer size */
	if (idx + size + size_dev_name + 3 + 1 > sec_tsp_log_size) {
		len = scnprintf(&sec_tsp_log_buf[0],
						size + size_dev_name + 3 + 1, "%s : %s", msg, buf);
		*sec_tsp_log_ptr = len;
	} else {
		len = scnprintf(&sec_tsp_log_buf[idx], size + size_dev_name + 3 + 1, "%s : %s", msg, buf);
		*sec_tsp_log_ptr += len;
	}
}
EXPORT_SYMBOL(sec_debug_tsp_log_msg);

static ssize_t sec_tsp_log_write(struct file *file,
					     const char __user *buf,
					     size_t count, loff_t *ppos)
{
	char *page = NULL;
	ssize_t ret;
	int new_value;

	if (!sec_tsp_log_buf)
		return 0;

	ret = -ENOMEM;
	if (count >= PAGE_SIZE)
		return ret;

	ret = -ENOMEM;
	page = (char *)get_zeroed_page(GFP_KERNEL);
	if (!page)
		return ret;;

	ret = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out;

	ret = -EINVAL;
	if (sscanf(page, "%u", &new_value) != 1) {
		pr_info("%s\n", page);
		/* print tsp_log to sec_tsp_log_buf */
		sec_debug_tsp_log(page);
	}
	ret = count;
out:
	free_page((unsigned long)page);
	return ret;
}


static ssize_t sec_tsp_log_read(struct file *file, char __user *buf,
								size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (sec_tsp_log_buf == NULL)
		return 0;

	if (pos >= *sec_tsp_log_ptr)
		return 0;

	count = min(len, (size_t)(*sec_tsp_log_ptr - pos));
	if (copy_to_user(buf, sec_tsp_log_buf + pos, count))
		return -EFAULT;
	*offset += count;
	return count;
}

static const struct file_operations tsp_msg_file_ops = {
	.owner = THIS_MODULE,
	.read = sec_tsp_log_read,
	.write = sec_tsp_log_write,
	.llseek = generic_file_llseek,
};

static int __init sec_tsp_log_late_init(void)
{
	struct proc_dir_entry *entry;
	if (sec_tsp_log_buf == NULL)
		return 0;

	entry = proc_create("tsp_msg", S_IFREG | S_IRUGO,
			NULL, &tsp_msg_file_ops);
	if (!entry) {
		pr_err("%s: failed to create proc entry\n", __func__);
		return 0;
	}

	proc_set_size(entry, sec_tsp_log_size);

	return 0;
}

late_initcall(sec_tsp_log_late_init);
#endif /* CONFIG_SEC_DEBUG_TSP_LOG */
