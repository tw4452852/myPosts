Read Groupcache | 2013-08-02 | go

groupcache可以说是[memcached](http://en.wikipedia.org/wiki/Memcached)的替代品.

这篇文章主要介绍groupcache的内部实现细节,主要有下面几个抽象的概念:

- 数据表现形式 - `ByteView`
- 数据存储容器 - `Sink`
- cache数据结构 - `LRU cache`
- 集群的表示 - `Group`和`HTTPPool`

下面分别对每个概念进行阐述.

## ByteView



`ByteView`实现了一个只读的`[]byte`,
其中内部的数据既可以是`string`也可以是`[]byte`, 从它的数据结构就可以看出:

~~~
type ByteView struct {
	b []byte
	s string
}
~~~
这里有几点需要强调下:

- 如果`b`非空,则优先使用`b`,否则使用`s`作为数据.
- `ByteView`在使用时是值类型, 这也就保证了其抽象了一个只读的`[]byte`

当然`ByteView`实现了`[]byte`和`string`一些基本方法,包括:
Len, ByteSlice, String, At, Slice, SliceFrom, Copy, Equal, EqualBytes, EqualString
除此之外,还实现了`io.ReadSeeker`和`io.ReadAt`.

## Sink



`Sink`抽象出一个数据存储容器,
无论该数据的形式是`string`、`[]byte`还是
[Protocol Buffer](http://en.wikipedia.org/wiki/Protocol_Buffers)编码.

`Sink`其实是个`interface`:

~~~
type Sink interface {
	// SetString sets the value to s.
	SetString(s string) error

	// SetBytes sets the value to the contents of v.
	// The caller retains ownership of v.
	SetBytes(v []byte) error

	// SetProto sets the value to the encoded version of m.
	// The caller retains ownership of m.
	SetProto(m proto.Message) error

	// view returns a frozen view of the bytes for caching.
	view() (ByteView, error)
}
~~~
这里主要有下面几个实例实现了`Sink interface`:
`stringSink`, `byteViewSink`, `protoSink`,
`allocBytesSink`, `truncBytesSink`

这里说下`allocBytesSink`和`truncBytesSink`的区别,
顾名思义,`truncBytesSink`在数据长度超过其limit时,
只会保存最多limit的数据,多出来的数据将会被丢弃,
这里的limit是有初始化时传入的slice的len决定的.

## LRU cache



LRU cache作为其内部的cache表现形式,先来看数据结构:

~~~
type Cache struct {
	// MaxEntries is the maximum number of cache entries before
	// an item is evicted. Zero means no limit.
	MaxEntries int

	// OnEvicted optionally specificies a callback function to be
	// executed when an entry is purged from the cache.
	OnEvicted func(key Key, value interface{})

	ll    *list.List
	cache map[interface{}]*list.Element
}
~~~
其中`cache`是一个hash map,用于存放`{key, value} pair`,
而`ll`为一个队列,队列中存放的是key, 那些最近被使用的keys分布在队列的头部,
而那些很久不用的keys则分布在队列尾部.

`MaxEntries`在初始化时设定,表示该cache最大能够容纳的pair的数量,
当数量不断激增超过该limit时,会删除那些很久没用的pairs,
当然用户也可以设定删除时的回调函数(`OnEvicted`).

## HTTPPool



`HTTPPool`主要是抽象1-N的映射关系,数据结构如下:

~~~
// HTTPPool implements PeerPicker for a pool of HTTP peers.
type HTTPPool struct {
	// Context optionally specifies a context for the server to use when it
	// receives a request.
	// If nil, the server uses a nil Context.
	Context func(*http.Request) Context

	// Transport optionally specifies an http.RoundTripper for the client
	// to use when it makes a request.
	// If nil, the client uses http.DefaultTransport.
	Transport func(Context) http.RoundTripper

	// base path including leading and trailing slash, e.g. "/_groupcache/"
	basePath string

	// this peer's base URL, e.g. "https://example.net:8000"
	self string

	mu    sync.Mutex
	peers []string
}
~~~
首先对于每个节点使用的base URL来标识自己,
那么选取对端节点呢,这里使用的是将用于查询的key的crc作为数组的index,
从peers中选出一个:

~~~
func (p *HTTPPool) PickPeer(key string) (ProtoGetter, bool) {
	// TODO: make checksum implementation pluggable
	h := crc32.Checksum([]byte(key), crc32.IEEETable)
	p.mu.Lock()
	defer p.mu.Unlock()
	if len(p.peers) == 0 {
		return nil, false
	}
	if peer := p.peers[int(h)%len(p.peers)]; peer != p.self {
		// TODO: pre-build a slice of *httpGetter when Set()
		// is called to avoid these two allocations.
		return &httpGetter{p.Transport, peer + p.basePath}, true
	}
	return nil, false
}
~~~
这里有2个TODO:

- 可以选择用于计算checksum的算法
- 预先定义httpGetter的slice,这样就不用每次申请了.

HTTPPool还有一个功能,就是响应查询的请求,
主要是从请求的url中提取出groupName和key,
然后在本地或者对端查询结果,并将结果返回给用户端.
具体的查询策略下面会有详述.

## Group



`Group`抽象出一个namespace,其中包括相关的数据
以及存储该数据的一台或多台机器.

~~~
type Group struct {
	name       string
	getter     Getter
	peersOnce  sync.Once
	peers      PeerPicker
	cacheBytes int64 // limit for sum of mainCache and hotCache size

	mainCache cache
	hotCache cache

	// loadGroup ensures that each key is only fetched once
	// (either locally or remotely), regardless of the number of
	// concurrent callers.
	loadGroup singleflight.Group

	// Stats are statistics on the group.
	Stats Stats
}
~~~
这里有2个cache:`mainCache`, `hotCache`,
为什么会有2个cache呢,这里有必要说下具体的查询策略:

当接收到一个查询请求,根据url中的groupName和key值在相应的group中查询:

1. `lookupCache`: 在`mainCache`和`hotCache`中查询.
2. `getFromPeer`: 根据key在相应的节点处查找, **可能**将查询结果放入`hotCache`中.
3. `getLocally`: 在本地计算value,并将结果放入`mainCache`中.

这里有2点需要说明:

- 第2步中,说的是可能,具体可以从代码中看出:

~~~

func (g *Group) getFromPeer(ctx Context, peer ProtoGetter, key string) (ByteView, error) {
	...
	// TODO(bradfitz): use res.MinuteQps or something smart to
	// conditionally populate hotCache.  For now just do it some
	// percentage of the time.
	if rand.Intn(10) == 0 {
		g.populateCache(key, value, &g.hotCache)
	}
	...
}
~~~

- 如果本地cached中没有该结果,
会首先从remote peer中去尝试获得,而不是先从本地计算获得?

其实这里正是Groupcache的要点所在,因为所有的数据是分布式的,
所以,如果本地没有,那么有可能在其他节点有,
那么就就没有必要在本地去重新计算结果,
更重要的是,这里本地计算的overhead是高于网络传输的overhead.
具体可以go-nuts上的[这篇讨论](https://groups.google.com/d/msg/golang-nuts/H0OwZC5TVcY/VmX0YfzEtiAJ).

FIN.
