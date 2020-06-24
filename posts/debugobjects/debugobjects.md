---
title: Debug objects framework
time: 09:54 15 Mar 2017
tags: linux, kernel
---

* background

Debug objects ( 以下简称do ) 是linux kernel对object生命周期追踪和操作验证的通用框架 .
它常被用于检查下面这些常见bug : 

1. 使用未初始化的object . 

2. 初始化正在使用的object . 

3. 使用已被销毁的object . 

下面结合代码来看看具体如何使用以及其内部的实现原理 . 


* api

do对外提供一些列的api , 用于外部模块告知object生命周期的变化 : 

1. `debug_object_init:` 用于告知object已被初始化 , 当该object正在被使用 ,
或者是之前已被销毁 , 那么do会给出warning , 同时 , 如果外部模块提供修正接口 , 
do会尝试修正 . 当然如果该object是第一次初始化 , 那么do会将其状态置位已初始化 . 
这里需要注意的是 , 该接口用于那些分配在heap上的object , 如果do发现该object
在调用者的stack上 , 同样会给出warning . 

2. `debug_object_init_on_stack:` 和 `debug_object_init` 类似 , 只不过该object
被初始化在stack上 .

3. `debug_object_activate:` 告知object正在被使用 , 如果该object已经被使用 ,
或者之前已被销毁， 那么do会给出warning , 并尝试修正 . 

4. `debug_object_deactivate:` 告知object停止使用 , 如果该object已经被停用 , 
或者已被销毁 , 那么do会给出warning.

5. `debug_object_destroy:` 告知object被销毁 , 如果该object正在被使用 ,
或者已被销毁 , 那么do会给出warning , 并尝试修正该错误 .

6. `debug_object_free:` 告知object被释放 , 如果该object正在被使用 , 
那么do会给出warning , 并尝试修正该错误 . 同时该接口还会将object在do中
的记录移除 . 

7. `debug_object_assert_init:` 用于断言object已被初始化 , 如果发现该object
还未被初始化 , 那么do会给出warning , 并尝试初始化该object . 

8. `debug_check_no_obj_freed:` 该接口用于检查一段已经释放的地址空间是否还存在
未被释放的object . 如果发现还有正在使用的object , 那么会将其释放并给出warning ,
若存在未被使用的object , 那么会将其记录从do中删除 . 

* implement

do内部对object的抽象结构为 `struct` `debug_obj:`

.code corpus /debug_obj_s OMIT/,/debug_obj_e OMIT

这些 `debug_obj` 被存放在全局的hash table中 : 

.code corpus /obj_hash_s OMIT/,/obj_hash_e OMIT

这里为每个bucket设置了一个lock是为了减少lock的contention . 

而hash的key的则为实际的object的地址/page_size :

.code corpus /hash_key_s OMIT/,/hash_key_e OMIT

所以同一个页内的object都在同一个bucket中 . 

而每个 `debug_obj` 都是从 `obj_pool` 中分配 , 
该pool最多容纳1024个 `debug_obj` , 最小需要保留256个 `debug_obj` : 

.code corpus /1s OMIT/,/1e OMIT

当pool中的 `debug_obj` 过多 , 会将多余的释放 `kmem_cache`  :

.code corpus /2s OMIT/,/2e OMIT

如果pool中空闲的 `debug_obj` 过少 , 那么会从 `kmem_cache` 中申请 : 

.code corpus /3s OMIT/,/3e OMIT

最后 , 关于do系统的初始化是分两部分来完成的 ,
最开始 `obj_pool` 是静态分配的 , 当kernel `kmem_caches` 使能之后 , 
所有的 `debug_obj` 都是动态分配的 , 之前静态的 `debug_obj` 会被释放 , 
取而代之的是动态分配一个用于替换原来的 . 

.code corpus /4s OMIT/,/4e OMIT

下面来看看 `debug_objects_replace_static_objects` 的实现 . 

首先从 `kmem_cache` 中动态分配空闲的 `debug_obj` : 

.code corpus /5s OMIT/,/5e OMIT

接着 , 将之前静态分配的 `debug_obj` 从 `obj_pool` 中删除 , 并将当前动态分配的
`debug_obj` 放进去 : 

.code corpus /6s OMIT/,/6e OMIT

最后 , 如果之前已经有了object被do记录 , 需要将这些object替换成我们动态申请的
`debug_obj` , 这里 , 通过遍历全局的hash table便可以得到当前有记录的object : 

.code corpus /7s OMIT/,/7e OMIT

* use case

最后 , 我们通过一个例子来看看如何使用do . 

这里模拟的场景是 : 释放一个正在使用object . 

.code corpus /8s OMIT/,/8e OMIT

使用do时 , 需要提供一个用于描述object的descriptor ,
其中 , 我们可以提供相关的修正函数 , 当do尝试修正某个错误时 , 
对应的callback会被调用 , 下面是该descriptor的定义 : 

.code corpus /9s OMIT/,/9e OMIT

这里 , 为了简单起见 , 我们只设置该object的名字以及当释放出错时的修正函数 : 

.code corpus /10s OMIT/,/10e OMIT

在释放修正函数中 , 我们强制设置object的状态为未使用 . 

这样 , 当程序运行时 , kernel会出现如下warning : 

.code corpus /11s OMIT/,/11e OMIT

可以看出 , do检测出我们正在free一个正被使用的object(status = 3 =
ODEBUG_STATE_ACTIVE) , 同时将call stack和相关
的context也dump出来 , 这样我们就可以很方便的定位出问题出现的位置 .

FIN.
