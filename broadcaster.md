[zz] Broadcast with linked channels | 2013-06-27
# Broadcast with linked channels

[原文在此](http://rogpeppe.wordpress.com/2009/12/01/concurrent-idioms-1-broadcasting-values-in-go-with-linked-channels/)

channel在go中是非常有用的工具,但是在有些情景下,效果不是很明显.
其中之一便是一对多的通信.channel在多个写者一个读者的情景下工作的很好.
但是如果有多个读者等待一个写者的情景下使用方法并不是很明显.

下面是通常情况下的实现的API:

~~~ 
type Broadcaster ...

func NewBroadcaster() Broadcaster
func (b Broadcaster) Write(v interface{})
func (b Broadcaster) Listen() chan interface{}
~~~
使用`NewBroadcaster`创建一个广播通道,通过`Write`方法广播一个值,
如果需要收听制定的广播通道,我们调用`Listen`方法,
它返回一个channel,我们便可以通过它接受广播的值.

我们可以立刻想到一个广播的实现方案:
有一个中间线程记录着所有的注册的接受线程,
每次调用`Listen`都会向其添加一个新的channel,
核心主线程的处理逻辑可能看上去是下面这样的:

~~~ 
for {
	select {
		case v := <-inc:
			for _, c := range(listeners) {
				c <- v
			}
		case c := <-registryc:
			listeners.push(c);
	}
}
~~~
这是一个通常的实现方案,当然也是最显而易见.
写线程将会一直阻塞直到所有的读线程接收之前的值,
对于该问题,一个解放方法便是为每个读线程维护一个buffer,
该buffer可以随着实际的情况变大,或者我们也可以在buffer满的情况下丢弃当前值.

当然,这篇文章并不是介绍通常的实现方案, 这里主要介绍一个写线程非阻塞的实现方案;
如果读线程慢而写线程快,并且该情形持续足够长的时间,那么内存会被耗尽.
当然该方案效率也不是非常高.

但是我并不关心那么多,我认为该方案非常有趣,也许有一天,我会真正使用它.

下面是该方案的核心部分:

~~~ 
type broadcast struct {
	c	chan broadcast
	v	interface{}
}
~~~
这就是我所谓的"链接管道"(和链表类似).但是它不仅仅是链表,它是一种[轮回数据结构][1].
也就是该结构的一个实例可以发送到他自身的管道中.

[1]: http://wadler.blogspot.com/2009/11/list-is-odd-creature.html

换句话说,如果我有一个`chan broadcast`,
那么我可以从其中读取一个`broadcast`类型的变量的`b`,
同时可以通过`b.v`得到广播的值,而`b.c`可以让我进行下一次的读取.

该迷惑的另一个方面来自使用buffer channel作为一对多广播的实体.
如果我有一个类型为T的buffer channel:

~~~ 
var c = make(chan T, 1)
~~~
那么任何尝试读取该channel的线程都会被阻塞,直到有一个值被写入.
当我们想要广播一个值,我们简单的将其写入channel.
这个值只会被一个读线程接收到,但是如果所有读线程都遵循这样一个规则:
每当从channel中读到一个值,都立即将其重新放回到channel中:

~~~ 
func wait(c chan T) T {
	v := <-c
	c <- v
	return v
}
~~~
将上述两部分合并在一起,我们可以看出,如果在`broadcast`中的channel是buffer的,
那么我们将得到一个一次性的一对多的广播流.

下面是整个的代码:

~~~ 
package broadcast

type broadcast struct {
	c	chan broadcast
	v	interface{}
}

type Broadcaster struct {
	// private fields:
	Listenc	chan chan (chan broadcast)
	Sendc	chan<- interface{}
}

type Receiver struct {
	// private fields:
	C chan broadcast
}

// create a new broadcaster object.
func NewBroadcaster() Broadcaster {
	listenc := make(chan (chan (chan broadcast)))
	sendc := make(chan interface{})
	go func() {
		currc := make(chan broadcast, 1)
		for {
			select {
			case v := <-sendc:
				if v == nil {
					currc <- broadcast{}
					return
				}
				c := make(chan broadcast, 1)
				b := broadcast{c: c, v: v}
				currc <- b
				currc = c
			case r := <-listenc:
				r <- currc
			}
		}
	}()
	return Broadcaster{
		Listenc: listenc,
		Sendc: sendc,
	}
}

// start listening to the broadcasts.
func (b Broadcaster) Listen() Receiver {
	c := make(chan chan broadcast, 0)
	b.Listenc <- c
	return Receiver{<-c}
}

// broadcast a value to all listeners.
func (b Broadcaster) Write(v interface{})	{ b.Sendc <- v }

// read a value that has been broadcast,
// waiting until one is available if necessary.
func (r *Receiver) Read() interface{} {
	b := <-r.C
	v := b.v
	r.C <- b
	r.C = b.c
	return v
}
~~~
该方案有个很好的地方:不再需要一个中心注册机制.
`Receiver`是对流中的一个位置的封装,并且可以在需要的时候进行复制-
每次复制都会得到一个单独的拷贝.当然这里也不需要进行注销.
不可否认的是,如果读线程跟不上写线程的速度,那么内存将会被无限制的使用.

下面是一个使用它的简单的示例代码:

~~~ 
package main

import (
	"fmt"
	"broadcast"
	"time"
)

var b = broadcast.NewBroadcaster()

func listen(r broadcast.Receiver) {
	for v := r.Read(); v != nil; v = r.Read() {
		go listen(r)
		fmt.Println(v)
	}
}

func main() {
	r := b.Listen()
	go listen(r);
	for i := 0; i  < 10; i++ {
		b.Write(i)
	}
	b.Write(nil)

	time.Sleep(3 * 1e9)
}
~~~
这里留着读者一个练习:在不运行代码的前提下,该程序将打印多少行？

以上是对原文的翻译，自己模仿其写了一个[package][2].

[2]: https://github.com/tw4452852/broadcast

FIN.
