---
title: GEM Memory Address Management - User Mode
time: 14:02 23 Dec 2014
tags: linux, drm, gfx, i915, gem
---

* 简介

这篇主要针对[[/post?q=GEM_Memory_Address_Management_-_Kernel_Mode][上一篇]]
所讲的 `kernel` 提供给用户层提供的两个接口,
来分析下用户空间对 `buffer` 的管理,
这里以 `intel` 实现的 `buffer-manager` 为例子, 以下简称 `bufmgr` .

* Cache Buckets

这里 `bufmgr` 按照 `buffer` 的大小分为一个个的 `bucket` ,
相同大小的,或者大小相差不大的 `buffer` 会被放入到同一个 `bucket` 中,
我们来看大小是如何分布的:

.code intel_bufmgr_gem.c 3182,3195

可以看出大小的分布是:
4k,8k,12k,16k,20k,24k,28k...
注意这里的分布不能按照等比的顺序,这样 `memory` 的占用会比较大.

* Allocation

当尝试分配一个 `buffer` ,我们首先尝试从 `cache` 中分配,
那么首先必须确定寻找的 `bucket` :

.code intel_bufmgr_gem.c 694

这里根据实际分配的大小,找到满足条件的最小的 `bucket`

.code intel_bufmgr_gem.c 366,372

如果找到了满足条件的 `bucket` ,下面就是从中寻找合适的 `cache` `buffer` 了,
这里考虑到 `GPU` `cache` ,对于那些充当 `render-target` 的 `buffer`
从尾部开始寻找,因为释放时都是从尾部添加的.

.code intel_bufmgr_gem.c 706,716

而对于非 `render-target` 的 `buffer` 选择从开头开始寻找:

.code intel_bufmgr_gem.c 725,736

注意这里除了保证大小满足条件,还需要保证该 `buffer` 当前处于 `unbusy` 的状态,
否则,我们可能分配了很多 `buffer` ,但是最终都在等待 `gpu` 处理完之后释放该
`buffer` .

由于是从 `cache` 中获得的 `buffer` ,
下面需要告诉 `kernel` 我们即将使用该 `buffer` ,
如果 `kernel` 已经将其占用的内存回收了,
我们需要真实释放掉该 `buffer`, 进行并重新寻找.

.code intel_bufmgr_gem.c 740,746

其实这里还将该 `bucket` 中的其他已经被 `kernel` 释放的 `buffer` 释放掉.
这是在 `drm_intel_gem_bo_cache_purge_bucket` :

.code intel_bufmgr_gem.c 634,645

如果最终我们没有从 `cache` `bucket` 中找到合适的 `buffer` ,
那么只能创建一个新的 `buffer` 了,
如果申请失败了,可能是由于的确没有足够的空间了,
这里,我们会将整个 `cache` `buckets` 都清空,
然后再重新尝试创建.

.code intel_bufmgr_gem.c 770,793

* Free

当一个 `buffer` 的引用计数为0时,那么需要释放该 `buffer` 了,
这里做了两件事,首先是释放该 `buffer`, 之后是根据时间清理
`cache` `buckets`:

.code intel_bufmgr_gem.c 1339,1350

注意,这里的时间单位为秒.

释放一个 `buffer` 时,首先会通知 `kernel` 该 `buffer` 对应的内存可以被回收,
如果 `kernel` 没有立即回收该内存,那么会将该 `buffer` 放入到对应的 `bucket`
的尾部

.code intel_bufmgr_gem.c 1310,1318

否则,直接将该 `buffer` 释放.

.code intel_bufmgr_gem.c 1319,1321

这里的目的主要是,在每次释放一个 `buffer` 之后
会遍历整个 `cache` `bucket` 将在之前1秒之前的所有的 `cache` 都释放掉:

.code intel_bufmgr_gem.c 1183,1199

FIN.
