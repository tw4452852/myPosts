---
title: Provisional Paging Setup in Linux
time: 18:00 19 Jan 2014
tags: linux, kernel, intel
---

* 概述

本文主要讲解下,linux kernel在实模式(real mode)中是如何建立所谓的临时页表的,
有了这些临时页表,便可以开启cpu页管理单元, 进入真正的保护模式(protect mode).
这里只考虑32位x86平台.

* 评估

在开始建立页表之前,我们有必要知道,我们到底需要映射多大的内存.
这里需要考虑几方面的要求:

- 能够容纳整个kernel image.
- 能够放下所有的页表项.
- 不应该映射过大的空间,因为这些映射只是临时的.

之所以说是临时,因为现在我们还不知道真实的物理内存是多大,
当知道了真实的物理内存大小,我们需要重新建立合适的页表.

先来看kernel image有多大,这个信息我们可以通过链接脚本来获得:

.code vmlinux.lds.S 82,119

这里有很多内容被省略了,我们来几个关键的导出符号:

- _text,_etext: kernel代码段的开始和结束地址.
- _sdata,_edata: kernel数据段的开始和结束地址.
- __brk_base,__brk_limit: kernel初始时堆的开始与结束地址,需要注意的是,kernel将所有的初始化好的页表都放在堆的开始处.

*NOTE:* 这里所说的地址都是指运行时的虚拟地址.

知道了image的大小,下面就是要计算所有页表项需要占用的空间.

.code head_32.S 67,68

由于kernel只能使用线性地址空间中从PAGE_OFFSET(0xc0000000)开始的1Gb(1<<32 -
0xc0000000)的空间,所以,我只需要计算这部分空间所占用的页表即可.

在代码中,这部分地址空间被称为LOWMEM,其所需的物理页的个数为:

.code head_32.S 64,65

下面需要考虑2中情况,一个是开启PAE功能,另一种是未开启的情形.

先来看PAE disable情形下,这种情形比较简答,因为是使用的2级分层模式,
所以只需要计算有几个页目录项即可,每个页目录项指向一个页表,
每个页表含有1024个页表项,每个页表项是对应物理页的物理地址,
所以,一个页表是4Kb,正好是一个物理页的大小,
那么我们只需要计算有几个页表即可:

.code head_32.S 61

那么如果PAE enable呢,这时采用的是3级分层模式,
首先我们任然需要一个页目录项,不过这时它指向的是一个页中间目录(PMD),
该表含有512个表项,每个表项8个字节,指向一个页表,
所以该表页正好占一个物理页的大小,那么我们这里除了和之前一样计算需要多少页表,
还需要再加上一个页中间目录:

.code head_32.S 59

* 页表初始化

知道了我们需要映射的大小,下面就是具体的页表初始化了.
先来开PAE enable的情形, 其中页中间目录是存放kernel的BSS段中的:

.code head_32.S 679,680

而页目录是存放在数据段中,其中有部分表项已被静态初始化(主要用于bios,不再本文讨论范围):

.code head_32.S 696,714

剩下的页表都是放在预留的堆空间(从__brk_base开始):

.code head_32.S 172,204

当PAE disable, 页目录是存放在BSS段中的:

.code head_32.S 682,683

页表初始化的过程和PAE enable时差不多:

.code head_32.S 207,239

* 开启页管理单元

万事俱备,只欠东风,最后一步便是开始页管理单元,
这里需要做2件事:

- 将页目录的物理地址放入cr3寄存器.
- 将cr0中的PG标记置1.

代码中也的确是这么做的:

.code head_32.S 394,397

FIN.