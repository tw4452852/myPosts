---
title: LXC in Android
time: 14:00 31 Jan 2016
tags: container,lxc,android
---

* introduction

`LXC` 是linux下的轻型虚拟化的一个解决方案,它可以基于操作系统实现环境的隔离,
这里和 `chroot` 有点像.
通常情况下,我们会将某些服务运行在其中,从而避免环境的污染.
这段时间,我主要实现的是在某个 `lxc` 中运行一个完整的Android系统.

* platform

因为 `lxc` 是在操作系统层面上的虚拟化(相比较而言,qemu则是基于硬件之上的虚拟化)
所以在开始有必要说一下宿主机的操作系统的选择,这里有两个选择:

- 使用android系统:优点是无须对kernel做适配,因为其本身就可以允许android系统.缺点则是android上的系统自带的工具较少,如特殊需要将某些工具porting到android上,这里就包括后面所说的lxc系列工具.
- 使用linux发行版系统:优点是系统工具很丰富,操作简单.缺点则是需要针对android系统做kernel的适配(比如binder驱动).

这里,我选择的事方案一,即使用android原生系统,在此基础上进行改造,实现在android系统上使用lxc,并最终实现在lxc
container中运行另一个android系统,同时运行于container中android系统可以同时运行,彼此之间没有相互影响.

* porting lxc tools

这里首先需要将lxc系列工具porting到android上.
首先下载lxc源码(https://github.com/lxc/lxc),然后编写对应的Android.mk
编译时会有一些错误,大多数是由于android使用的是bionic libc而原生是基于glibc.
注意调整config.h的一些宏即可.

当lxc系列工具编译出来,放入android系统中,便可以通过lxc-create创建container,
这里需要注意的是这里需要修改默认的lxc_dir,因为android系统的一些文件系统结构
和标准linux不同.

同时,这里还需要创建一个android版本的lxc template,它主要用于生成container的
rootfs和config,这里可以基于一些现有的模板修改,也可以自己写一个.

在适配的过程中,发现了一个bug,该bug是通过lxc-info查看container状态时发现的,
有些情况下,container的ip会显示不出来,最后发现是因为代码中在获取接口状态信息时,
有memory overflow. 具体参见:https://github.com/lxc/lxc/pull/743

* binder driver

有了lxc工具,创建好android的rootfs和config,首先需要解决binder driver的适配
问题,binder主要用于进程间通信,而binder进程德管理是由一个全局的context manager
复杂,由于每个容器有属于自己的context manager,所以这里需要将context manager
和namespace进行关联(这里主要是pid namespace).
当某个binder进程需要和其他进行通信,我们需要知道当前进程所属的pid namespace,
然后找到对应的context manager.
这里的解决方案是当创建context manager时,获取当前的pid namespace,并将新创建的
context manager与namespace关联,并放入全局的链表中进行管理.
后续便可以通过pid namespace 找到对应的context manager.

* display driver

android 底层是使用framebuffer,通常情况下它与显存是一一对应的,所以当我们
启动多个container时,他们是共享framebuffer的,彼此之间并不知道对方,
所以framebuffer的内容势必是错乱的,这里需要防止不同的container同时对framebuffer进行访问,
这里便浮现出一个方案,可以将不同container对framebuffer的操作进行序列化,
同一时刻只有一个container可以对framebuffer进行操作,当发生操作切换时,
需要更新userspace对framebuffer的映射,使其指向一个该容器独占的虚拟的framebuffer,
同时真实的framebuffer上内容也需要更新,和新换入的container的虚拟framebuffer的内容一致.
这个方案,在container数量很多时,会带来大量的framebuffer切换,从而带来
大量内存拷贝,效率上存在问题.

所以这里需要为每个container分配属于自己的framebuffer,相互之间没有影响,
当然这里的framebuffer并不是真实的,是我们虚拟的,所以userspace的操作
并不会影响到真实的显存,所以屏幕上并不会有任何输出,我们只有通过vnc
等类似远程桌面的形式连接上,才能看到实际德屏幕画面.
因为这些userspace应用获取是我们虚拟的framebuffer.

同样,这里也需要将虚拟的frambuffer与pid namespace进行绑定,并进行统一管理.

* IO

android的IO是通过event的方式与kernel进行交互,
而kernel中具体是input子系统将底层硬件(device)上报的信息
封装成event,并分发给所有的收听者(handler).

显然,该模型无法满足多container的情形,设想如果一个键盘
按键按下,所有的container都会收到该event,
所以我们需要对event按照container进行分发.

这里的解决方案是为每个event添加一个pid namespace信息,
而每个event handler也同样会有一个pid namespace信息,
input系统会根据event和handler的pid namespace进行分发,
从而隔离不同container的event.

** input simulation

通过刚开我们的分析,不同的container实际上是共享底层的io设备的,
我们还需要为每个container虚拟出属于他们自己的io设备,
这里我们可以使用uinput实现userspace模拟硬件的io事件,
比如keybord、touchscreen等等.

同样,在下发模拟的event时,需要附加其所属的pid namespace信息,
这样input子系统就可以防止该event影响其他的container.

FIN.
