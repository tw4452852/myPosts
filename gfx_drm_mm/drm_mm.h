/**************************************************************************
 *
 * Copyright 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX. USA.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/
/*
 * Authors:
 * Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#ifndef _DRM_MM_H_
#define _DRM_MM_H_

/*
 * Generic range manager structs
 */
#include <linux/list.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>
#endif

struct drm_mm_node {
	struct list_head node_list; // 在drm_mm的全局链表中的位置
	struct list_head hole_stack; // 若该节点后面存在空洞，在表示在drm_mm的hole_stack的位置
	unsigned hole_follows : 1; // 该节点后面是否存在空洞
	unsigned scanned_block : 1; // 检查器是否检查过该节点
	unsigned scanned_prev_free : 1; // 暂时没用
	unsigned scanned_next_free : 1; // 暂时没用
	unsigned scanned_preceeds_hole : 1; // 保存在检查之前该节点的前一个节点是否有空洞，用于状态恢复
	unsigned allocated : 1; // 该节点是否被分配使用
	unsigned long color; // 用于地址调整的颜色值
	unsigned long start; // 占用的地址范围的开始
	unsigned long size; // 占用的地址范围的结束
	struct drm_mm *mm; // 指向所属的分配器
};

struct drm_mm {
	struct list_head hole_stack; // 所有含有空洞的节点的链表
	struct drm_mm_node head_node; // 所有的已分配的节点的链表, 按照起始地址升序排序
	struct list_head unused_nodes; // drm_mm_node结构体的cache链表
	int num_unused; // cache链表中的个数
	spinlock_t unused_lock; // 用于保护cache链表
	unsigned int scan_check_range : 1; // 用户是否指定地址的范围
	unsigned scan_alignment; // 检查的对齐要求
	unsigned long scan_color; // 检查的color设置
	unsigned long scan_size; // 希望的空洞的大小
	unsigned long scan_hit_start; // 标记找到的空洞的起始地址
	unsigned long scan_hit_end; // 标记找到的空洞的尾地址
	unsigned scanned_blocks; // 记录经过检查的节点的个数
	unsigned long scan_start; // 用户指定的范围的开始地址
	unsigned long scan_end; // 用户指定的范围的结束地址
	struct drm_mm_node *prev_scanned_node; // 所有经过检查的节点的链表

	void (*color_adjust)(struct drm_mm_node *node, unsigned long color,
			     unsigned long *start, unsigned long *end); // 对符合的条件的节点进行可选的地址范围的调整
};

static inline bool drm_mm_node_allocated(struct drm_mm_node *node)
{
	return node->allocated;
}

static inline bool drm_mm_initialized(struct drm_mm *mm)
{
	return mm->hole_stack.next;
}

static inline unsigned long __drm_mm_hole_node_start(struct drm_mm_node *hole_node)
{
	return hole_node->start + hole_node->size;
}

static inline unsigned long drm_mm_hole_node_start(struct drm_mm_node *hole_node)
{
	BUG_ON(!hole_node->hole_follows);
	return __drm_mm_hole_node_start(hole_node);
}

static inline unsigned long __drm_mm_hole_node_end(struct drm_mm_node *hole_node)
{
	return list_entry(hole_node->node_list.next,
			  struct drm_mm_node, node_list)->start;
}

static inline unsigned long drm_mm_hole_node_end(struct drm_mm_node *hole_node)
{
	return __drm_mm_hole_node_end(hole_node);
}

#define drm_mm_for_each_node(entry, mm) list_for_each_entry(entry, \
						&(mm)->head_node.node_list, \
						node_list)
#define drm_mm_for_each_scanned_node_reverse(entry, n, mm) \
	for (entry = (mm)->prev_scanned_node, \
		next = entry ? list_entry(entry->node_list.next, \
			struct drm_mm_node, node_list) : NULL; \
	     entry != NULL; entry = next, \
		next = entry ? list_entry(entry->node_list.next, \
			struct drm_mm_node, node_list) : NULL) \

/* Note that we need to unroll list_for_each_entry in order to inline
 * setting hole_start and hole_end on each iteration and keep the
 * macro sane.
 */
#define drm_mm_for_each_hole(entry, mm, hole_start, hole_end) \
	for (entry = list_entry((mm)->hole_stack.next, struct drm_mm_node, hole_stack); \
	     &entry->hole_stack != &(mm)->hole_stack ? \
	     hole_start = drm_mm_hole_node_start(entry), \
	     hole_end = drm_mm_hole_node_end(entry), \
	     1 : 0; \
	     entry = list_entry(entry->hole_stack.next, struct drm_mm_node, hole_stack))

/*
 * Basic range manager support (drm_mm.c)
 */
extern int drm_mm_reserve_node(struct drm_mm *mm, struct drm_mm_node *node);
extern struct drm_mm_node *drm_mm_get_block_generic(struct drm_mm_node *node,
						    unsigned long size,
						    unsigned alignment,
						    unsigned long color,
						    int atomic);
extern struct drm_mm_node *drm_mm_get_block_range_generic(
						struct drm_mm_node *node,
						unsigned long size,
						unsigned alignment,
						unsigned long color,
						unsigned long start,
						unsigned long end,
						int atomic);

static inline struct drm_mm_node *drm_mm_get_block(struct drm_mm_node *parent,
						   unsigned long size,
						   unsigned alignment)
{
	return drm_mm_get_block_generic(parent, size, alignment, 0, 0);
}
static inline struct drm_mm_node *drm_mm_get_block_atomic(struct drm_mm_node *parent,
							  unsigned long size,
							  unsigned alignment)
{
	return drm_mm_get_block_generic(parent, size, alignment, 0, 1);
}
static inline struct drm_mm_node *drm_mm_get_block_range(
						struct drm_mm_node *parent,
						unsigned long size,
						unsigned alignment,
						unsigned long start,
						unsigned long end)
{
	return drm_mm_get_block_range_generic(parent, size, alignment, 0,
					      start, end, 0);
}
static inline struct drm_mm_node *drm_mm_get_block_atomic_range(
						struct drm_mm_node *parent,
						unsigned long size,
						unsigned alignment,
						unsigned long start,
						unsigned long end)
{
	return drm_mm_get_block_range_generic(parent, size, alignment, 0,
						start, end, 1);
}

extern int drm_mm_insert_node(struct drm_mm *mm,
			      struct drm_mm_node *node,
			      unsigned long size,
			      unsigned alignment);
extern int drm_mm_insert_node_in_range(struct drm_mm *mm,
				       struct drm_mm_node *node,
				       unsigned long size,
				       unsigned alignment,
				       unsigned long start,
				       unsigned long end);
extern int drm_mm_insert_node_generic(struct drm_mm *mm,
				      struct drm_mm_node *node,
				      unsigned long size,
				      unsigned alignment,
				      unsigned long color);
extern int drm_mm_insert_node_in_range_generic(struct drm_mm *mm,
				       struct drm_mm_node *node,
				       unsigned long size,
				       unsigned alignment,
				       unsigned long color,
				       unsigned long start,
				       unsigned long end);
extern void drm_mm_put_block(struct drm_mm_node *cur);
extern void drm_mm_remove_node(struct drm_mm_node *node);
extern void drm_mm_replace_node(struct drm_mm_node *old, struct drm_mm_node *new);
extern struct drm_mm_node *drm_mm_search_free_generic(const struct drm_mm *mm,
						      unsigned long size,
						      unsigned alignment,
						      unsigned long color,
						      bool best_match);
extern struct drm_mm_node *drm_mm_search_free_in_range_generic(
						const struct drm_mm *mm,
						unsigned long size,
						unsigned alignment,
						unsigned long color,
						unsigned long start,
						unsigned long end,
						bool best_match);
static inline struct drm_mm_node *drm_mm_search_free(const struct drm_mm *mm,
						     unsigned long size,
						     unsigned alignment,
						     bool best_match)
{
	return drm_mm_search_free_generic(mm,size, alignment, 0, best_match);
}
static inline  struct drm_mm_node *drm_mm_search_free_in_range(
						const struct drm_mm *mm,
						unsigned long size,
						unsigned alignment,
						unsigned long start,
						unsigned long end,
						bool best_match)
{
	return drm_mm_search_free_in_range_generic(mm, size, alignment, 0,
						   start, end, best_match);
}

extern void drm_mm_init(struct drm_mm *mm,
			unsigned long start,
			unsigned long size);
extern void drm_mm_takedown(struct drm_mm *mm);
extern int drm_mm_clean(struct drm_mm *mm);
extern int drm_mm_pre_get(struct drm_mm *mm);

static inline struct drm_mm *drm_get_mm(struct drm_mm_node *block)
{
	return block->mm;
}

void drm_mm_init_scan(struct drm_mm *mm,
		      unsigned long size,
		      unsigned alignment,
		      unsigned long color);
void drm_mm_init_scan_with_range(struct drm_mm *mm,
				 unsigned long size,
				 unsigned alignment,
				 unsigned long color,
				 unsigned long start,
				 unsigned long end);
int drm_mm_scan_add_block(struct drm_mm_node *node);
int drm_mm_scan_remove_block(struct drm_mm_node *node);

extern void drm_mm_debug_table(struct drm_mm *mm, const char *prefix);
#ifdef CONFIG_DEBUG_FS
int drm_mm_dump_table(struct seq_file *m, struct drm_mm *mm);
#endif

#endif
