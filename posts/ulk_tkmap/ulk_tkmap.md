---
title: Temporary kernel mapping
time: 11:57 02 Mar 2014
tags: linux, kernel
---

* 简介

本篇紧接着[[/post?q=Permanent_kernel_mapping][上一篇]],
继续讲解kernel中对高端内存访问的第二种技术:
Temporary mapping.

从上一讲我们可以看出,permanent mapping是有可能阻塞的,
等待其他线程释放一些页表资源,显然,在一些场景下,
这种等待是不能接受,比如在中断回调函数中.

这样,temporary mapping就被引入进来,它是非阻塞的,
当然,它也有它的不足:页表的生命周期是暂时.

* 窗口

和permanent mapping类似,我们在kernel地址空间的顶端
预留了一些窗口(实质是页表项),专门用于temporary mapping,
当然这些窗口的个数是有限.

其中,每个CPU只有一定数量(KM_TYPE_NR)的窗口供其使用:

.code kmap_types.h 4,8

这里的__WITH_KM_FENCE是为了做debug用.

那么,我们如何通过窗口得到它所对应的虚拟地址,
或者通过虚拟地址得到对应的窗口呢.
这里有两个宏分别与之对应:

.code fixmap.h 190,191

这里的FIXADDR_TOP可以先认为就是可寻址的最大地址
(32bit: 2**32, 64bit: 2**64).

说到这里,就不能不说下fix mapping了,
顾名思义,他建立的映射的,
为什么会有这样的映射呢,因为有些地址是有特殊用途的,
而且也是固定的,那么我们就必须将这些地址保留下来,
不能让它用于其他的映射,
在kernel中,这些地址都有一个枚举变量与之对应:

.code fixmap.h 74,150

当然,其中就有为temporary mapping预留的地址空间:

.code fixmap.h 109,110

所以只要我们有了这个枚举变量,通过上述的两个宏,
就可以得到其对应的虚拟地址,反之亦然.

* 申请和释放

申请和释放对应的API分别为kmap_atomic和kunmap_atomic.
对于申请和释放,最主要的工作就是找到一个空闲的页表项,
和permanent mapping不同,这里不能等待.
在具体的实现中,是通过一个原子操作得到这个index的:

申请时:

.code highmem_32.c 43

释放时:

.code highmem_32.c 91

这里每个cpu有自己的一个全局变量,用于标识当前使用的index,

.code highmem.h 88

当申请时,只是将该计数原子+1,如果其超过了最大的可用个数,
那么kernel认为这是一个bug.

.code highmem.h 90,99

同理,释放时该值原子-1:

.code highmem.h 106,115

有了这个index,下面就是得到其在全局的枚举变量中的位置:

.code highmem_32.c 44

以及其对应的虚拟地址:

.code highmem_32.c 45

最终更新对应的页表项:

.code highmem_32.c 46,47

这里有两点需要注意:

- 如果该页表项不为空,说明有人正在使用,那么kernel认为这是个bug.
- 全局变量kmap_pte对应用于temporary mapping的最后一个页表项.

释放的过程和申请的过程同理,首先是得到当前使用的页表项在全局枚举变量中的位置:

.code highmem_32.c 78,79

接着是清空对应的页表项:

.code highmem_32.c 90

最终释放用于表示当前CPU使用的index的计数:

.code highmem_32.c 91

FIN.
