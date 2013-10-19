Read Go - Channel | 2013-06-05
# Read Go - Channel

今天主要说说Go中channel的实现机制,其中主要有以下几个部分:

- Channel的基本数据结构.
- channel的同步和异步收发.
- select的实现.

## Essential data structure



无论channel的发送方还是接收方都可能互相等待对方,
所以,理所当然有2个等待队列,一个用于发送方,一个用于接收方.

~~~ 
struct	Hchan
{
	...
	WaitQ	recvq;			// list of recv waiters
	WaitQ	sendq;			// list of send waiters
	...
};
~~~
可想而知,等待队列中都是一些处于等待状态的G.

对于buffer channel还必须有个buffer,
这里的设计采用的是一个环形的ring,典型的生产者和消费者的模型.

~~~ 
struct	Hchan
{
	uintgo	qcount;			// total data in the q
	uintgo	dataqsiz;		// size of the circular q
	uint16	elemsize;
	...
	uintgo	sendx;			// send index
	uintgo	recvx;			// receive index
	...
};
~~~
这里有个需要主要的是,这个buffer是预先分配的,
在内存中的分布是紧跟着channel描述符(struct Hchan)之后的.

~~~ 
// Buffer follows Hchan immediately in memory.
// chanbuf(c, i) is pointer to the i'th slot in the buffer.
#define chanbuf(c, i) ((byte*)((c)+1)+(uintptr)(c)->elemsize*(i))

Hchan*
runtime·makechan_c(ChanType *t, int64 hint)
{
	...
	// allocate memory in one call
	c = (Hchan*)runtime·mal(n + hint*elem->size);
	c->elemsize = elem->size;
	c->elemalg = elem->alg;
	c->elemalign = elem->align;
	c->dataqsiz = hint;
	...
}
~~~

## sync/async send and receive



知道了基本的数据结构,下面我们来看下数据收发的基本流程.
由于发送和接收有可能导致当前的G进入sleep状态,
不过这里有个例外,如果调用方需要知道该操作是否真的进行了发送还是接收,
会传入一个bool型的指针用于返回结果,
如果该指针不为nil,那么该G并不会进入sleep,而是直接返回.
反之,则G会进入sleep,等待别人将其唤醒.

下面假设该指针不为nil.

### send



**case 1: send on nil channel**

当channel为nil时,当前G会进入永久睡眠的状态.

**case 2: send on closed channel**

当channel被close,则按照语言规范中要求的,会产生一个runtime panic

**case 3: sync send**

在该情况下,会首先查看`recvq`中是否有处于等待G,
如果存在,会将其从队列中取出,同时将发送的数据copy到等待的G的接收buffer中,
最后将其状态从`Gwaiting`变为`Grunnable`,使其重新被调度运行.

如果`recvq`为空,那么会将当前的G放入到`sendq`中,
然后其状态变为`Gwaiting`等待有新的接收G将其唤醒.

**case 4: async send**

在异步的情况下,如果buffer已经满了,同样会使当前的G进入等待.
如果buffer还有剩余的空间,便将发送的数据copy到buffer中,
然后尝试唤醒`recvq`中存在的G, 因为现在已经有新的元素被放入到channel buffer中了.

### receive



**case 1: receive on nil channel**

当channel为nil时,当前G也会进入永久睡眠的状态.

**case 2: receive on closed channel**

如果该channel被close,这里直接将nil作为返回值返回.

**case 3: sync receive**

在情绪下的处理逻辑和sync类似,首先会尝试唤醒`sendq`的G,
如果没有再将当前的G放入到`recvq`等待队列中.

**case 4: async receive**

这里,如果buffer为空,当前G会被放入`recvq`中进入等待,
反之从buffer中取出一个元素,同样的,这里也会尝试唤醒`sendq`中的G,
因为现在channel buffer中已经有剩余的空间容纳新的内容.

## select



select可以看做对一组channel的send/receive,
和一个channel的receive/send不同的是,
这里需要遍历所有的channel,从而选择一个符合条件的channel,
那么这里就会有2个问题需要解决:

- 对一组channel遍历的顺序性.
- 对一组channel操作的原子性.

下面将一一解答这两个问题.

首先是channel遍历的顺序性,简单的说就是我们按照什么顺序去遍历这些channel,
这里需要考虑随即性,
这里采用的是伪随即的方式将之前的先后顺序随即打乱.

~~~ 
static void*
selectgo(Select **selp)
{
	...
	// generate permuted order
	for(i=0; i<sel->ncase; i++)
		sel->pollorder[i] = i;
	for(i=1; i<sel->ncase; i++) {
		o = sel->pollorder[i];
		j = runtime·fastrand1()%(i+1);
		sel->pollorder[i] = sel->pollorder[j];
		sel->pollorder[j] = o;
	}
	...
}
~~~

然后是原子性的问题,很显然这里需要使用锁来保护对channel的操作,
不过由于这里同时存在多个channel,所以需要对lock/unlock的顺序进行规范,
否则会导致死锁.

对于锁的顺序,这里有个比较trick的设计,按照channel描述符(Hchan)的地址从小到大的顺序进行排序,
之后按照经过排序的顺序依次进行lock/unlock,
所以虚拟地址小的channel会先被锁住.

~~~ 
static void*
selectgo(Select **selp)
{
	...
	// sort the cases by Hchan address to get the locking order.
	// simple heap sort, to guarantee n log n time and constant stack footprint.
	for(i=0; i<sel->ncase; i++) {
		j = i;
		c = sel->scase[j].chan;
		while(j > 0 && sel->lockorder[k=(j-1)/2] < c) {
			sel->lockorder[j] = sel->lockorder[k];
			j = k;
		}
		sel->lockorder[j] = c;
	}
	for(i=sel->ncase; i-->0; ) {
		c = sel->lockorder[i];
		sel->lockorder[i] = sel->lockorder[0];
		j = 0;
		for(;;) {
			k = j*2+1;
			if(k >= i)
				break;
			if(k+1 < i && sel->lockorder[k] < sel->lockorder[k+1])
				k++;
			if(c < sel->lockorder[k]) {
				sel->lockorder[j] = sel->lockorder[k];
				j = k;
				continue;
			}
			break;
		}
		sel->lockorder[j] = c;
	}
	...
}
~~~

知道这两点,下面就是如何这一组的channels进行send/receive了,
这里同样也是依次遍历每个channel,只要在遍历的过程中有一个channel
进行了send/receive,那么select就直接返回,后面的channel将不会被遍历.
而单个channel的send/receive之前已经说过了,这里就不再赘述了.
唯一需要的注意的是如果所有的条件都没有满足(无论是sync还是async的send/receive)
那么当前的G会进入等待,而在等待之前,会统一将当前的G放入到所有case的waitq中(当然,这里需要根据case的类型放入恰当的队列中recvq/sendq)
当G被某一个成功的case重新唤醒之后,会将所有失败的case中,之前的放入的G再取出来,
只保留成功的那一个case.

FIN.
