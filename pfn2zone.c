#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/mmzone.h>
#include <linux/mm.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JoeJa");

//unsigned long long pfn = 0;

static unsigned long long pfn_array[3000];
static int pfnArrayCnt = 0;

//module_param(pfn, ullong, S_IRUSR);
//MODULE_PARM_DESC(pfn, "Page frame number");

module_param_array(pfn_array, ullong, &pfnArrayCnt, 0000);
MODULE_PARM_DESC(pfn_array, "An array of Page Frame Number");

void getZone(unsigned long long pfn){
	struct page *pageObj;
	struct zone *pageZone;
	
	//printk(KERN_INFO "NID: %d=>%d\n", pfn_valid(pfn), pfn_to_nid(pfn));
	if(pfn_valid(pfn)){
		pageObj = pfn_to_page(pfn);
		pageZone = page_zone(pageObj);
		// printk(KERN_INFO "Zone Num: %d\t", page_zonenum(pageObj));
		// printk(KERN_INFO "Zone: %s\n", pageZone->name);
		printk(KERN_INFO "pfn2zone_by_Joe 0x%llx 0x%lx %d %s\n", pfn, pageObj->flags, page_zonenum(pageObj), pageZone->name);
	}else{
		printk(KERN_INFO "pfn2zone_by_Joe 0x0 0x0 -1 Invalid\n");
	}
}

static int __init pfn2zone_init(void)
{
	int i;
	
	//printk(KERN_INFO "Page file number: %llx\n", pfn);
	printk(KERN_INFO "CNT=%d\n", pfnArrayCnt);
	for (i = 0; i < pfnArrayCnt; ++i){
		getZone(pfn_array[i]);
	}
	
	return 0;
}

static void __exit pfn2zone_exit(void)
{
	//printk(KERN_INFO "Goodbye, world\n");
}

module_init(pfn2zone_init);
module_exit(pfn2zone_exit);
