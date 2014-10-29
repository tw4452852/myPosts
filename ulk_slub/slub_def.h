#ifndef _LINUX_SLUB_DEF_H
#define _LINUX_SLUB_DEF_H

/*
 * SLUB : A Slab allocator without object queues.
 *
 * (C) 2007 SGI, Christoph Lameter
 */
#include <linux/kobject.h>

enum stat_item {
	ALLOC_FASTPATH,		/* Allocation from cpu slab */
	ALLOC_SLOWPATH,		/* Allocation by getting a new cpu slab */
	FREE_FASTPATH,		/* Free to cpu slab */
	FREE_SLOWPATH,		/* Freeing not to cpu slab */
	FREE_FROZEN,		/* Freeing to frozen slab */
	FREE_ADD_PARTIAL,	/* Freeing moves slab to partial list */
	FREE_REMOVE_PARTIAL,	/* Freeing removes last object */
	ALLOC_FROM_PARTIAL,	/* Cpu slab acquired from node partial list */
	ALLOC_SLAB,		/* Cpu slab acquired from page allocator */
	ALLOC_REFILL,		/* Refill cpu slab from slab freelist */
	ALLOC_NODE_MISMATCH,	/* Switching cpu slab */
	FREE_SLAB,		/* Slab freed to the page allocator */
	CPUSLAB_FLUSH,		/* Abandoning of the cpu slab */
	DEACTIVATE_FULL,	/* Cpu slab was full when deactivated */
	DEACTIVATE_EMPTY,	/* Cpu slab was empty when deactivated */
	DEACTIVATE_TO_HEAD,	/* Cpu slab was moved to the head of partials */
	DEACTIVATE_TO_TAIL,	/* Cpu slab was moved to the tail of partials */
	DEACTIVATE_REMOTE_FREES,/* Slab contained remotely freed objects */
	DEACTIVATE_BYPASS,	/* Implicit deactivation */
	ORDER_FALLBACK,		/* Number of times fallback was necessary */
	CMPXCHG_DOUBLE_CPU_FAIL,/* Failure of this_cpu_cmpxchg_double */
	CMPXCHG_DOUBLE_FAIL,	/* Number of times that cmpxchg double did not match */
	CPU_PARTIAL_ALLOC,	/* Used cpu partial on alloc */
	CPU_PARTIAL_FREE,	/* Refill cpu partial on free */
	CPU_PARTIAL_NODE,	/* Refill cpu partial from node partial */
	CPU_PARTIAL_DRAIN,	/* Drain cpu partial to node partial */
	NR_SLUB_STAT_ITEMS };

struct kmem_cache_cpu {
	void **freelist;	// 指向下一个可用的空闲对象
	unsigned long tid;	// 全局唯一的操作id
	struct page *page;	// 当前slab所属的物理页
	struct page *partial;	// 在当前cpu本地,部分空闲的slab 链表
#ifdef CONFIG_SLUB_STATS
	unsigned stat[NR_SLUB_STAT_ITEMS]; // 状态统计信息
#endif
};

/*
 * Word size structure that can be atomically updated or read and that
 * contains both the order and the number of objects that a slab of the
 * given order would contain.
 */
struct kmem_cache_order_objects {
	unsigned long x;
};

struct page {
	...
	struct {
		union {
			...
			void *freelist;		// 指向该物理页上的下一个空闲的object
			...
		};

		union {
			unsigned counters; // 相关的计数,具体见下面3个字段

			struct {
				union {
					...
					struct {
						unsigned inuse:16; // 该物理页上正在使用的object的个数
						unsigned objects:15; // 该物理页上可以容纳的object的个数
						unsigned frozen:1; // 该物理页是否被锁定
					};
					...
				};
				...
			};
			...
		};
	};

	union {
		...
		struct {
			struct page *next;	// 指向下一个slab
			int pages;	// 在该物理页之后还有多少个空闲的slab
			int pobjects;	// 在该物理页之后剩余的空闲的object的个数
		};
		...
	};

	union {
		...
		struct kmem_cache *slab_cache;	// 指向所属的cache
		...
	};
	...
}
/*
 * Slab cache management.
 */
struct kmem_cache {
	struct kmem_cache_cpu __percpu *cpu_slab; // cpu本地节点数组
	unsigned long flags; // slub相关的标志
	unsigned long min_partial; // 每个NUMA node上至少存在的部分空闲的slab
	int size;		// 每个object的大小+其他记录信息
	int object_size;	// 每个object的大小
	int offset;		// 指向下个空闲object的指针据开头的偏移
	int cpu_partial;	// 每个cpu本地最多存放的object的个数
	struct kmem_cache_order_objects oo; // 每个slab中的object的个数

	/* Allocation and freeing of slabs */
	struct kmem_cache_order_objects max; // 每个slab中最多存放的object的个数
	struct kmem_cache_order_objects min; // 每个slab中至少存放的object的个数
	gfp_t allocflags;	// 分配实际物理页时的标志
	int refcount;		// 对该cache的引用的计数
	void (*ctor)(void *); // 用于初始化每个object
	int inuse;		// 每个object中实际使用的大小
	int align;		// 对齐要求
	int reserved;		// 在每个slab末尾保留的大小
	const char *name;	// 名字
	struct list_head list;	// cache 链表
	struct kmem_cache_node *node[MAX_NUMNODES]; // 每个NUMA node节点数组
};

#endif /* _LINUX_SLUB_DEF_H */
