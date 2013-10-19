[译] Understanding the Go Memory Model | 2013-05-30

# [译] Understanding the Go Memory Model

[原文在此](https://docs.google.com/document/d/163LwGViH_RSv5YEHmwixBJEjOOi3LYJbMhKY7sq3z1Q/edit?usp=sharing)

官方的[Go Memory Model](golang.org/ref/mm)(以下简称MM)
是一篇描述关于Go内存访问和goroutine通信中语言内建为你保证的相关内容.
但是,这是一个非常技术性的话题,有必要对它进行深入的理解.
在这篇文档中,我将尝试提炼出一些收获,
当然,这些收获来自这些年中irc和邮件列表对其的讨论.

## 之前发生



当阅读内存模型这篇文档,有一个核心概念,那就是"之前发生",
同时也是推导出正确理论的基础.

在MM,只有少数几个"之前发生"的关系(下文通过#n来引用):

1. 在一个goroutine中,源代码中表达式的顺序决定一个表达式在另一个表达式"之前发生".
2. 在当前包的init执行之前,被导入的包的init函数已经执行完成,
同时,所有的init函数在main函数开始之前发生.
3. 一个`go 表达式`在相应的goroutine执行开始之前发生.
4. 一个channel的send操作在那些从该channel读取的数据被求值之前发生.
5. 一个channel的close操作在那些从该channel读取的数据被求值为0之前发生.
6. 一个unbuffered channel的接收操作在该channel上相对应的发送操作完成之前发生.
7. 对一个忽斥量的解锁操作的调用在即将发生的对其加锁操作调用返回之前发生.
8. Once函数调用的完成在相应的Do操作返回之前发生.

就这么几点,而且其中的大多数看上去理所当然.
不幸的是,这并没有那么简单.
因为,一个操作在另一个操作之前发生,并不代表其对应的指令在另一个指令之前被CPU执行,
它真正的含义是:该操作不能被编译器或者CPU重排序,如果被重排序会导致程序的行为和语言规范描述的行为不符.
在MM中举了个例子来阐述了这点:
如果一个goroutine中有这样的表达式`a = 1; b = 2`,
在另外一个goroutine中可以会观察到对b的赋值在对a的赋值之前发生,
这中现象好像违反直觉,因为`a = 1`在`b = 2`之前发生,
但是`a = 1`的边际效应不会对`b = 2`产生影响,
所以在真正的内存写操作的顺序可能和源代码中表达式书写的顺序不符.

## 写观察



当存在一个全局共享数据,任何正确的形式推导都基于一个前提:对其的读操作能观察到对其的写操作所造成的改变.
但是正如墙上的时钟表现的那样,虽然秒针的移动在我们看时间之前发生,
但是这并不能保证我们看到的是改变之后的时间.
关于这个差别对于处理器和编译器优化并执行我们的程序至关重要.

MM中指出,只有满足以下2个条件,一个对v的读操作(`r`)才**被允许**能观察到另一个对v的写操作(`w`):

1. 该读操作(`r`)不在该写操作(`w`)之前发生.
2. 没有其他的写操作(`w'`)在该写操作(`w`)之后,同时在该写操作(`r`)之前发生.

注意这里说的是**被允许**,它并不意味这你将观察到写操作.
实际上,这里只是告诉你使用这些条件来保证共享内存的访问是不充分的,
因为当满足这些条件(或者只满足其中的某些条件)时,潜在的数据竞态已经存在.
(译者注:可能会有其他的写操作(`w'`)和当前的写操作(`w`)或者当前的读操作(`r`)同时发生)

为了确保没有数据竞态的发生,MM给出了一对相似的条件.
当这些条件满足时,一个对v的读操作(`r`)才**被确保**能观察到另一个对v的写操作(`w`):

1. 该写操作(`w`)在该读操作(`r`)之前发生.
2. 对于其他的写操作(`w'`), 要么在该写操作(`w`)之前发生,
要么该读操作(`r`)在`w'`之前发生.

从这2个条件可以看出:
如果只是说写操作在读操作之前发生,并不能保证写操作被读操作观察到.
同时还必须保证在写操作和读操作之前不能有其他写操作发生.
更专业点的说法就是,对一个内存地址的load操作返回的值是在之前最近一次对该内存地址store操作进入的值.
在一个没有数据竞态的程序中,必须只有1个这样的store操作.

这里有一个因为不正确的同步机制导致的边际效应:
因为对一个变量的初始化操作和对该变量的其他操作并没有保证明确的"之前发生"关系,
那么对一个变量的读操作可能对该变量的初始化操作在同一时间发生,
那么就有可能读到一个没有被初始化的值(不一定是0).

## 错误的信号量

让我们来看看下面这个错误的信号量的实现:

~~~ {prettyprint lang-go}
type broken chan bool
func (ch broken) acquire() { ch <- true }
func (ch broken) release() { <-ch }
func newSemaphore(count int) broken {
	return make(broken, count)
}
~~~

这个信号量的实现利用了buffer channel的容量来限制在临界区中goroutines的数量.

为了便于理解,我们通过一个具体的资源管理的问题来阐述:

~~~ {prettyprint lang-go}
type semaphore interface { acquire(); release() }
type work struct {
	sem semaphore
	i   int
}
func (w *work) execute() {
	go func() {
		w.sem.acquire()
		defer w.sem.release()
		// do work
	}()
}

~~~
假设现在`sem`字段为之前实现的错误的信号量的一个实例.

首先让我们来列举下上述代码中存在的"之前发生"的关系:

1. `sem.acquire`在`do work`之前发生,当然也在`sem.release`之前发生.(见#1)
2. `ch <- true`在`<-ch`之前发生.(见#4)

如果每次`sem.acquire`都在`sem.release`之前被调用,
那么,由`sem.acquire`填入channel的值应该和由`sem.release`从channel中获得的值对应.
但是由于这种"之前发生"的关系只存在于当前goroutine内部,
所以这显然和其他goroutines不同步,也有可能和程序中的其他部分同时发生.
编译器,runtime,处理器同样可以对这些声明和指令进行重排序,
只要经过重排序产生的行为变化不违反规范定义的即可.
(在当前情形下,只要满足对`sem.acquire`调用在对`sem.release`的调用之前发生即可)
这就可能导致一些或者全局的`do work`操作在临界区之外发生.
事实上,一个激进的优化可能会将`acquire/release`连在一起,
然后意识到对channel的send操作后紧接着receive等效成no-op操作,
那么优化器很有可能将整个信号量从程序中去除!

如果你想深入的分析为什么这种信号量是错误的,并且想了解MM中其他值得关注的内容,
看一看[这个邮件列表的讨论][0], 尤其是[这一段][1], [还有这一段][2].

[0]: https://groups.google.com/forum/#!topic/golang-dev/ShqsqvCzkWg/discussion
[1]: https://groups.google.com/forum/#!msg/golang-dev/ShqsqvCzkWg/9WGj2yPK9xYJ
[2]: https://groups.google.com/forum/#!msg/golang-dev/ShqsqvCzkWg/Kg30VPN4QmUJ

## 一个正确的信号量

幸运的是,对上述错误的信号量实现的修改是简单的:

~~~ {prettyprint lang-go}
type working chan bool
func (ch working) acquire() { <-ch }
func (ch working) release() { ch <- true }
func newSemaphore(count int) working {
	sem := make(working, count)
	for i := 0; i < count; i++ {
		sem <- true
	}
}
~~~

现在,让我们重新看下这里存在"之前发生"的关系:

- `sem.acquire`在`sem.release`之前发生.(见#1)
- 对于`sem.acquire`的前n次调用(`1 <= n <= count`):
在`newSemaphore`中第n次send在该次的receive之前发生.(见#4)
- 对于`sem.acquire`的第m次调用(`m > count`):
第`m - count`次`sem.release`中的send在该次的receive之前发生.(见#4)

从这里可以看出,当`sem.acquire`中的receive操作完成时,
且之前没有调用过`sem.release`,
最多只可能有`count - 1`次对`sem.acquire`的调用.
换句话说,同时最多只能有`count`个goroutines在临界区中.
由于在该例子中,同步机制发生在goroutines之间,
"之前发生"关系可以被应用到数据访问上:
例如,如果这里的`count`为1,那么该信号量可以被当作忽斥量使用.

## Futures



作为对内存访问正确推理的一个事例,下面是一个关于future的一个[实现][1]:

[1]: http://play.golang.org/p/FOFIDeR6EJ

~~~ {prettyprint lang-go}
type FutureInt struct {
	value int
	ready chan struct{}
}

func NewFutureInt(provider func() int) *FutureInt {
	f := &FutureInt{
		ready: make(chan struct{}),
	}
	go func() {
		defer close(f.ready)
		f.value = provider() // A
	}()
	return f
}

func (f *FutureInt) Get() int {
	<-f.ready
	return f.value // B
}
~~~

首先,先来我们希望有那些保证:
B处对`f.value`的读操作能够观察到A处对`f.value`的赋值.
如果我们能够保证这点,那么这个future的实现就是正确的.

下面是一些我们可以从MM中得出的保证:

- 对`f.value`的写操作在对`f.ready`的关闭操作之前发生.(见#1)
- 对`f.ready`的读操作在对`f.value`的读操作之前发生.(见#1)
- 对`f.ready`的关闭操作在相关的读操作之前发生.(见#5)

从以上这些关系,可以看出B处对`f.value`的读操作在A处对`f.value`的写操作之后发生.
回忆之前说过的,为了让B处能够读到A处写入的值,只有个条件本身是不充分的,
还必须保证其他的写才做要不然在A之前发生,要不然在B之后发生.
在该实现中,对`f.value`的写操作只有A一处,
所以在基于该实现只在自己包中且用户无法修改`f.value`的前提下,
不可能有其他的写操作和A处的写操作同时发生.

由于两个"之前发生"的条件都满足,所以B处的读可以观察到A处的写,
所以该实现是正确的.

## Once



下面是一个错误的尝试快路延迟初始化的例子,该例子来自MM:

~~~ {prettyprint lang-go}
var a string
var done bool

func setup() {
	a = "hello, world"
	done = true
}

func doprint() {
	if !done {
		once.Do(setup)
	}
	print(a)
}

func twoprint() {
	go doprint()
	go doprint()
}

~~~
从上述代码中可以得出如下理论:

- 对`a`的写操作在对`done`的写操作之前发生.(见#1)
- 对`done`的读在对`a`的读之前发生.(见#1)

这里只需要保证:在`done == false`下,Do调用在对a的读操作之前发生.
但是,事实上,编译器是可以对`setup`中的指令进行重排序的,
因为这样修改之后产生的行为变化并不违反语言规范所要求的.
同样需要注意的是,从系统内存一致性的角度说,
对`a`的写操作和对`done`的写操作在别的线程来看可能发生在不同的时刻,
MM中通过下面的例子阐述了这点:如果一个goroutine中有这样的表达式`a = 1; b = 2`,
另一个goroutine中可能观察到对`b`的写操作在对`a`的写操作之前.

这里有2中情形.
当`done == false`,对`a`的写在函数返回之前发生(见#1),
同时`setup`函数的返回也在`Do`返回之前发生(见#8),
最后,`Do`返回在对a的读操作之前发生(见#1).
`sync.Once`元语保证其函数只会被执行一次,
所以没有其他对a的写操作同时发生,
所以在该情形下,`doprint`可以被保证输出`Hello world`.
但是当`done == true`,就没有任何的同步,
所以对`a`的读和其他的一切都有可能同时发生.
由于该情形和前一种情形可能同时发生,所以,
它可能只输出部分,或者全局,或者压根就没有观察到前一个情形中对`a`的写操作.

这里同样需要提及的是,`sync.Once`本身已经提供了快路,
所以这里的实现是没有必要的.

## Reader Close



另一个不合适的同步的例子来自[the reader closing a channel][1].
对一个已经关闭的channel进行写操作,根据语言规范可知会导致panic.
这里,依据MM并没有提供任何的保证,`panic`和导致它触发的关闭操作同时发生,
同样不能得出关于内存一致性的任何推到.
作为一个例子,一个足够高级的runtime也不能立即重新调度一个等待写入一个关闭的channel的goroutine.
同样,在reader中对内存的写操作也不能保证被writer观察到.

如果需要reader告知writer它不再需要了或者不能在接收更多的数据,
添加一个供发送者等待的channel,同时当读者不再需要数据时,关闭该channel即可.([实现代码][2])
同样,该机制也可以用于由于writer速度太快,reader将数据pushback的情形.
通过使用这样的channel,reader和所有的writers的通信变得明确,
同时也可以根据内存一致性得出正确的推导.

[1]: http://play.golang.org/p/xsWuDKDbvO
[2]: http://play.golang.org/p/gIijxcFfi2

## 互斥



关于MM中使用`sync.Mutex`实现互斥的概念是一个很有趣的例子.
这里只有对`Unlock`的调用发生在对`Lock`的调用之前.
这可能对顺序操作不是很有用,由于它还需要对`Lock`的调用保证顺序性.
这里对保证"写观察"很有用.对于保证写观察的第2个条件:
没有其他的写操作在该写操作之后,或者读操作之前发生.
如果所有的对一块共享内存区域的读操作和写操作在`Lock`之后`Unlock`之前,
(也就是,由该锁保护的临界区),那么剩下的就是保证临界区的顺序的"之前发生"的关系,
那么该"写观察"的保证就是完整.

## 总结



Go的MM明确指出不提供顺序一致性.简单的说就是,
在源代码中书写的顺序并不意味这发生的顺序就是那样,
除非有明确的同步机制来保证.
我所见过的最常见的2个例子就是:
使用一个共享变量(通常是一个布尔类型的叫做`done`或者`ready`)来表明一个goroutine的状态,
通常该状态有另一个goroutine来检查.
还有就是利用buffer channel的容量来实现同步.

学习区别正确并且明确的同步和基于不正确的顺序一致性的代码可能会花费时间.
如果你需要一个全面的单元或者集成测试(你通常需要写这样的测试),
那么[Race Detector][1]是一个帮助你找出不正确的同步机制的无价的工具,
因为它根据内存访问机制得出正确的推论,
同时在程序运行或者测试的过程中报告出任何存在的竞态.

[1]: http://golang.org/doc/articles/race_detector.html
