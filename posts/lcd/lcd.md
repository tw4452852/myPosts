---
title: Linux LCD driver
time: 14:33 08 Nov 2013
tags: linux,kernel,driver
---

* 简介

这篇文章主要分析linux中的lcd驱动,在linux一个lcd显示屏被抽象成 帧缓冲(`framebuffer`) 的概念.

在开始之前,首先说下lcd tft屏的成像原理.

** 时序

.image timing.png

*** 像素

这里假设一个像素点由16bit组成:

- *VD[23:19],VD[15:10],VD[7:3]:* 分别对应R,G,B分色信息,一个16bit的组合代表了一个象素的信息
- *VDEN:* 数据使能信号
- *VCLK:* 显示指针从矩形左上角的第一行第一个点开始,一个点一个点的在LCD上显示,在上面的时序图上用时间线表示就为VCLK,我们称之为像素时钟信号

*** 行

显示指针在从一行的右边回到下一行的左边是需要一定的时间的,我们称之为行切换,
理想情况下,两行之间不需要间隔,也就是说这一行结束马上传输下一行的第一个pixel的VD.
但是这样做并不好,因为一个点的偏差会造成满盘皆输.因此我们引入了行同步HSYNC信号,也就是说在传输完一行的数据后,先歇一会儿,等待若干个
时钟(我们称之为后插入等待周期HFPD);然后我们送一个行同步信号,当然这个信号的有效周期数我们也能控制(我们称之为同步周期HSPW);之后呢,我们在等一会,让
LCD驱动电路准备好接收,我们在把一行的数据发下去(这个等待时间我们称之为前插入等待周期HBPD)

- *HSYNC:* 当显示指针一直显示到矩形的右边就结束这一行,那么这一行的动作在上面的时序图中就称之为1 Line,如此类推,显示指针就这样一行一行的显示至矩形的右下角才把一副图显示完成.因此,这一行一行的显示在时间线上看,就是时序图上的HSYNC,表示新的一行即将开始
- *HSPW(horizontal*sync*pulse*width):* 表示水平同步信号的宽度,用VCLK计算
- *HBPD(horizontal*back*porch):* 表示从水平同步信号开始到一行的有效数据开始之间的VCLK的个数
- *HFPD(horizontal*front*porth):* 表示一行的有效数据结束到下一个水平同步信号开始之间的VCLK的个数

*** 帧

同理,帧同步的原理和行同步一样,也存在3个周期:

- *VSYNC:* 然而,LCD的显示并不是对一副图像快速的显示一下,为了持续和稳定的在LCD上显示,就需要切换到另一幅图上(另一幅图可以和上一副图一样或者不一样,目的只是为了将图像持续的显示在LCD上).那么这一副一副的图像就称之为帧,在时序图上就表示为1 Frame,因此从时序图上可以看出1 Line只是1 Frame中的一行;同样的,在帧与帧切换之间也是需要一定的时间的,我们称之为帧切换,那么LCD整个显示的过程在时间线上看,就可表示为时序图上的VSYNC,表示新的一帧即将开始
- *VBPD(vertical*back*porch):* 表示在一帧图像开始时,垂直同步信号以后的无效的行数
- *VFBD(vertical*front*porch):* 表示在一帧图像结束后,垂直同步信号以前的无效的行数
- *VSPW(vertical*sync*pulse*width):* 表示垂直同步脉冲的宽度,用行数计算

** 帧缓冲(framebuffer)

了解了lcd的成像基本原理和时序,下面我们结合具体代码,来看下具体是如何完成这些操作的.

首先来看下帧缓冲设备驱动在Linux子系统中的结构:

.image framebuffer.png

我们从上面这幅图看,帧缓冲设备在Linux中也可以看做是一个完整的子系统,
大体由fbmem.c和xxxfb.c组成.向上给应用程序提供完善的设备文件操作接口(即对FrameBuffer设备进行read、write、ioctl等操作),
接口在Linux提供的fbmem.c文件中实现;向下提供了硬件操作的接口,只是这些接口Linux并没有提供实现,因为这要根据具体的LCD控制器硬件进行设置,
所以这就是我们要做的事情了(即xxxfb.c部分的实现)

*** 重要数据结构

**** fb_info

最主要的数据结构为 `fb_info` ,
其中包括了帧缓冲设备的全部信息,包括设备的设置参数,
状态以及对底层硬件操作的函数指针.

.code fb.h /FB_INFO START OMIT/,/FB_INFO END OMIT/

其中,比较重要的成员有 `struct`fb_var_screeninfo`var` ,
`struct`fb_fix_screeninfo`fix` 和`struct`fb_ops`*fbops` ,他们也都是结构体

**** fb_var_screeninfo

`fb_var_screeninfo` 结构体主要记录用户可以修改的控制器的参数,比如屏幕的分辨率和每个像素的比特数等,该结构体定义如下:

.code fb.h /FB_VAR_SCREENINFO START OMIT/,/FB_VAR_SCREENINFO END OMIT/

**** fb_fix_screeninfo

而 `fb_fix_screeninfo` 结构体又主要记录用户不可以修改的控制器的参数,
比如屏幕缓冲区的物理地址和长度等,该结构体的定义如下:

.code fb.h /FB_FIX_SCREENINFO START OMIT/,/FB_FIX_SCREENINFO END OMIT/

**** fb_ops

`fb_ops` 结构体是对底层硬件操作的函数指针,该结构体中定义了对硬件的操作有:

.code fb.h /FB_OPS START OMIT/,/FB_OPS END OMIT/

*** 设备初始化

下面来看下当驱动模块加载时对设备初始化的过程,
这里拿s3c2410的lcd驱动代码作为例子,
设备初始化的代码在驱动的 `probe` 函数中.

.code s3c2410fb.c 1096,1105

首先看下这个回调函数的参数

.code s3c2410fb.c 816,817

一个指向 `platform_device` 的指针,想必是只想该驱动支持的设备的指针,
这部分工作是由 `platform_bus`
这条虚拟总线子系统去完成的,这部分内容不在此分析,
我们可以先这么认为:
在系统初始化的过程中,当探测到相应的设备,便会虚拟出一个 `platform_device`
并将其注册.
那么,我们来看看系统注册了那些可能的s3c2410的lcd设备.
在arch/arm/plat-samsung/devs.c中:

.code -numbers devs.c 845,859

这里主要预定义了访问设备的io地址和所用的中断号.
当然,对于lcd设备,还有一些特有的设置信息,例如屏幕有多大,分辨率是多少等等.
这些信息在arch/arm/mach-s3c24xx/mach-smdk2440.c:

.code -numbers mach-smdk2440.c 109,150

这样就既有操作系统相关的设置(io地址,中断号),
又有具体硬件平台相关的设置(分辨率,屏幕大小等等).
当然,平台相关信息是存放在 `platform_device->dev.platform_data` 中.

所以 `probe` 回调函数就可以根据预定义的设置进行初始化,
初始化的过程包括io地址空间的申请:

.code s3c2410fb.c 861,881

中断初始化:

.code s3c2410fb.c 915,920

申请帧缓冲区dma内存:

.code s3c2410fb.c 947,953

如果申请成功,那么 `fb_info->screen_base` 指向该段内存的起始虚拟地址,
`fb_info->fix.smem_start` 则指向该段内存的起始物理地址.

最终注册初始化好的 `framebuffer` :

.code s3c2410fb.c 971,976

** 调色板

在计算机图像技术中,一个像素的颜色是由它的R,G,B分量表示的,
每个分量又经过量化,一个像素总的量化级数就是这个显示系统的颜色深度.
量化级数越高,可以表示的颜色也就越多,最终的图像也就越逼真.
当量化级数达到16位以上时,被称为真彩色.但是,量化级数越高,就需要越高的数据宽度,
给处理器带来的负担也就越重;量化级数在8位以下时,所能表达的颜色又太少,不能够满足用户特定的需求.

为了解决这个问题,可以采取调色板技术.
所谓调色板,就是在低颜色深度的模式下,在有限的像素值与RGB颜色之间建立对应关系的一个线性表.
以256色调色板为例,调色板中存储256种颜色的RGB值,每种颜色的RGB值是16位.
用这256种颜色编制索引时,从00H~FFH只需要8位数据宽度,而每个索引所对应的颜色却是16位宽度的颜色信息.

下面来看下调色板相关的代码.
首先是在 `probe` 中对调色板的初始化:

.code s3c2410fb.c 912,913

设置调色板中的颜色是通过 `fp_ops.fb_setcolreg` 完成的.
在分析这个函数之前,有个概念必须先了解:

根据每个像素点bit位数(bpp)的不同,分为3种模式,
各个模式使用的调色板是不同的:

- 真彩色: bpp>=12, 使用软件调色板
- 单色: bpp==1, 不使用调色板
- 伪彩色: 1<bpp<12, 使用硬件调色板

硬件调色板和软件调色板,从字面上就可以猜出他们的区别,
当然他们分别对应 `fb_info` 结构中的2个字段:

- 硬件调色板 <-> `fb_info->palette_buffer`
- 软件调色板 <-> `fb_info->pseudo_palatte`

下面我们可以来看 `fb_setcolreg` 了,
这个函数的逻辑很简单,对于不同的模式,填充不同的调色板,
首先是真彩色模式:

.code s3c2410fb.c 492,504

接着是伪彩色模式:

.code s3c2410fb.c 506,518

其中, `schedule_palette_update` 是干什么的,这里有个优化,
对于调色板的更新,并不是每次更新一个,而是每次更新整个调色板,
而更新的时机便是在一次 `FRSYNC` 中断中,
所以这里只是将颜色的值先写入 `palette_buffer` 中,同时开启中断:

.code s3c2410fb.c 457,466

再反观,中断回调函数, 如果调色需要更新,则更新整个调色板:

.code s3c2410fb.c 753,759

** 其他

- `fb_ops->fb_check_var:` 主要是根据屏幕的分辨率( `xres`, `yres`) 和色宽( `bits_per_pixel` ), 获得相应屏幕的默认配置.
- `fb_ops->fb_set_par:` 使能相应的配置, 包括显示模式,行宽以及配置控制器寄存器组( `lcdcon1-5`, `lcdsaddr1-3`)
- `fb_ops->fb_blank:` 空屏
下面是一些操作和硬件设备没有依赖,所以使用了一系列通用的函数( `cfb_xxx` )

- `fb_ops->fb_fillrect:` 画一个单色矩形
- `fb_ops->fb_copyarea:` 复制一块区域
- `fb_ops->fb_imageblit:` 画一个画面

FIN.
