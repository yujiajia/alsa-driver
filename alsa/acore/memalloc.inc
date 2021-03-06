#include "alsa-autoconf.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#if defined(CONFIG_MODVERSIONS) && !defined(__GENKSYMS__) && !defined(__DEPEND__)
#include "sndversions.h"
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
#define CONFIG_HAS_DMA 1
#endif

#include <linux/pci.h>
#define SKIP_HIDDEN_MALLOCS 1
#include "adriver.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0) && defined(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
static inline struct proc_dir_entry *create_proc_read_entry(const char *name,
	mode_t mode, struct proc_dir_entry *base, 
	read_proc_t *read_proc, void * data)
{
	struct proc_dir_entry *res=create_proc_entry(name,mode,base);
	if (res) {
		res->read_proc=read_proc;
		res->data=data;
	}
	return res;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#undef WARN_ON
#define WARN_ON(condition) ({						\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on)) {					\
		printk("BUG: warning at %s:%d/%s()\n", __FILE__,	\
			__LINE__, __FUNCTION__);       			\
		dump_stack();	  					\
	}								\
	unlikely(__ret_warn_on);					\
})
#endif

// vim: ft=c
