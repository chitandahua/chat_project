# 每日总结:C++ 异步网络编程 & 后端工程实践

涵盖话题:Asio 异步模型、协程、并发保护、内存序、无锁编程、CMake/Conan 工程化、gRPC 异步集成、Boost.MySQL/Redis 生态坑。按主题整理,标注了容易踩坑的地方。

---

## 一、Asio 异步模型基础

### 底层原理:Reactor vs Proactor
- **Reactor**(epoll):内核只负责"就绪通知"("你现在可以读/写了"),真正的系统调用还是应用层自己发起
- **Proactor**(IOCP / io_uring):内核直接完成整个 I/O 操作,通知的是"结果",不是"许可"
- `async_write`/`async_read` 内部逻辑:**先尝试非阻塞系统调用 → 不行才注册 epoll 事件等通知 → 通知来了再重试**,不是每次都固定走 `epoll_ctl`
- `async_write`(无 `_some` 后缀)是**组合操作**,内部循环调用 `async_write_some` 直到全部数据发完才触发外层回调;`_some` 版本只保证"这一次系统调用"

### `io_context` 多线程调度
- 默认**一个 `io_context` 只有一个 epoll 实例**,不是每个线程各开一个
- 多线程模型:共享同一个就绪队列 + 单一 epoll,谁空闲谁去 `epoll_wait`,天然实现负载均衡(不是每线程独立分工)
- **易错**:同一个 fd 上,Asio 只保证"一读一写并发安全",**不保证两个并发写/两个并发读安全**——多个来源想写同一个 socket,必须靠队列 + 单一消费者协程,或者 strand 串行化,不能各自直接发起 `async_write`

### `work_guard`
- 作用:防止 `io_context::run()` 在"暂时没活干"时提前退出(线程池刚启动、还没分配到任务时最容易踩)
- 优雅关闭:`work_guard.reset()`(允许自然排空退出);强制关闭:`io_context.stop()`(立即打断,不等排空)

---

## 二、协程(C++20 `awaitable` + 有栈协程 `yield_context`)

### 两种协程机制对比
| | C++20 `awaitable`/`co_await` | 有栈协程 `yield_context` |
|---|---|---|
| 机制 | 编译器生成状态机,堆分配协程帧 | 真实用户态栈切换(依赖 Boost.Context) |
| 语法 | 需要 `co_await`/`co_return`,返回类型固定 `awaitable<T>` | 普通函数签名,最后一个参数传 `yield_context` |
| 出错处理 | 默认抛异常,`redirect_error`/`as_tuple`/`as_single` 可切换成错误码 | `yield` 抛异常,`yield[ec]` 走错误码,同一套设计理念 |
| C++20 之前用得多吗 | 不适用(需要 C++20) | **不是主流**,回调仍是 C++20 前的绝对主流,有栈协程只在数据库客户端、游戏后端等少数场景常见 |

### 易错点集中营

**1. 协程里不能用裸 `return;`,必须 `co_return;`**——只要函数体出现 `co_await`,整个函数就变成协程,普通 `return` 不再合法。

**2. 锁的作用域内绝对不能出现 `co_await`**——协程挂起时锁还攥着不放,如果恢复时被调度到另一个线程,`unlock()` 由非加锁线程调用是未定义行为;而且持锁时间可能无限拉长,卡住其他等锁的人。

**3. 判空+挂起等待之间不能有真正的挂起点(丢失唤醒 bug)**——`timer_`(模拟条件变量)的正确用法要求"判空"和"发起 `async_wait`"必须在**同一段不可打断的执行片段**内完成,中间任何一次真挂起都可能导致：`deliver()` 在判空之后、挂起之前插队调用 `cancel_one()`,这次通知打在"还没真正登记等待"的 timer 上,直接丢失,写协程从此永久卡死。
   - 根源类比:等价于 `std::condition_variable::wait(lock)` 必须持锁调用——检查条件和挂起必须是原子的一步
   - 修复:整个 `writer()` 协程通过 `co_spawn(strand_, ...)` 绑到 strand 上,而不是只在协程内部局部临时借用 strand

**4. `concurrent_channel` 默认容量是 0(同步握手语义)**——`try_send` 只有在**这一刻已经有消费者在等**才会成功,否则立即失败、消息静默丢弃(返回值是 bool,容易忘记检查)。构造时务必传一个合理容量(几十到上百)。

**5. `asio::buffer()` 不能直接塞自定义类型**——只认"连续内存+已知长度",自定义结构体(如 `MsgNode`)必须显式给 `data()`+`length()`。

**6. 继承接口时子类方法签名必须精确匹配基类纯虚函数**——参数类型不一致(如基类 `const std::string&` vs 子类 `const MsgNode&`)不算重写,子类依然是抽象类,`make_shared` 会报 "invalid new-expression of abstract class"。

---

## 三、并发保护三兄弟:mutex / strand / concurrent_channel

### 核心区别
- **mutex**:阻塞式,无竞争时开销极低(一两条原子指令),但持锁线程被抢占会导致其他等待者真正阻塞卡住(长尾延迟的常见来源)
- **strand**:非阻塞队列语义,`post()` 强制排队、`dispatch()` 满足条件时可内联执行;跟协程天生兼容(挂起不会破坏正确性),但队列投递本身有开销
- **concurrent_channel**:官方为多生产者单消费者场景专门优化,通常综合性能最好(实测比 strand 版吞吐高 ~23%)

### 实测数据参考(100 连接 / body 64B / 本机 loopback)
| 版本 | 吞吐 req/s | avg延迟(ms) |
|---|---|---|
| 单线程 | ~37k~43k | ~2.3~2.7 |
| 多线程多 io_context(完全隔离) | ~89k(**最高**) | ~1.1 |
| 单 io_context + mutex | ~84k | ~1.17 |
| 单 io_context + strand | ~81k | ~1.2 |
| 协程 + concurrent_channel | ~76k | ~1.3 |

**结论**:单线程→多线程收益最大;完全隔离的多 io_context 模型在负载均匀场景下略优于共享模型(但负载不均时未必);mutex 平均性能常略优于 strand,但尾延迟更差更不稳定。

### `post` vs `dispatch` vs `defer`
- `post`:总是排队,即便调用者就在目标线程上也不会插队立即执行
- `dispatch`:调用线程恰好是目标线程时立即内联执行,否则退化成 post
- `defer`:类似 post,但更倾向"当前调用栈结束后"执行

---

## 四、内存序(memory_order)

- **release/acquire 是对称的"单向屏障"**:release 前面**任何读写**都不能被重排到它之后;acquire 后面**任何读写**都不能被重排到它之前——不是只管写/只管读,常见简化说法容易误导。
- **`fetch_sub` 等 RMW 操作,原子性与内存序标签无关**——多个线程并发 `fetch_sub`,返回的旧值保证互不相同,这是硬件/标准保证,不依赖选择哪种内存序,内存序只影响"跟其他内存操作之间的同步关系"。
- **`compare_exchange_strong` 允许分传 success/failure 两个内存序**,因为 CAS 成功是真正的读写、失败只是纯读探测,两者需要的同步语义不同;`failure` 不能包含 release 语义,且强度不能超过 `success`。

---

## 五、无锁编程的现实判断

- **绝大多数业务场景,`mutex` + `atomic<int>` + 现成并发库就够用**,无锁结构写错的代价(极难复现的生产 bug)远超收益
- 真正值得自己设计无锁结构的场景:高频交易、游戏引擎热路径、OS/DB 内核、大厂专职基础库团队——共同特征是"锁本身经过 profiling 确认是热点 + 访问极度高频短小 + 团队能承担高验证成本"
- 现成库:`moodycamel::ConcurrentQueue`(事实标准无锁队列)、`Boost.Lockfree`、Intel TBB
- 无锁栈的经典坑:提前断开 `next` 主要是为了**防止递归析构爆栈**(链表太长导致析构连锁反应炸调用栈),不是直接为了防并发访问;并发访问 `next` 这类普通成员字段的风险,需要风险指针(Hazard Pointer)或拆分引用计数才能真正堵上,纯 `shared_ptr` 只保证引用计数本身线程安全

---

## 六、工程化:CMake / Conan / proto

- **Conan 优先下载 ConanCenter 预编译二进制,本地没有才现场编译**(`--build=missing` 才允许现场编译)——新版本编译器(如 gcc 16)常常没有现成二进制,需要现场编译或换稍旧的库版本
- `cmake_layout` 会让生成文件多一层 `build/Release/generators` 嵌套,配合 `cmake --preset conan-release` 使用最省心
- **proto 生成放进 `build` 目录而非源码树**:用 `add_custom_command` + 显式列出输出文件(不要用 `GLOB` 扫生成目录,因为文件可能还不存在,`GLOB` 是配置期一次性求值)
- **多个子项目共享 proto**:用 `if(TARGET xxx)` 判断 target 是否已存在,已存在则直接复用,避免重复生成/编译
- **编译慢的应对**:预编译头(PCH,`target_precompile_headers`,多个 target 间用 `REUSE_FROM` 共享)+ ccache + proto 生成代码拆成独立静态库(不随业务代码改动重新编译)+ `-j$(nproc)` 并行编译

---

## 七、gRPC 异步集成

- **同步 `Service` 基类的方法必须在 return 前就有结果**,逼得只能在方法体里阻塞等(`.get()`),这本身没问题,但**必须确保调用它的线程不是驱动相关 `io_context` 的那个线程**,否则死锁
- **`CallbackService` + `ServerUnaryReactor`** 才是真正非阻塞的方案:方法立即返回 reactor,真正结果通过后续调用 `reactor->Finish(status)` 通知,可以来自任意线程/任意时刻
- 迁移到 CallbackService 的两处强制改动:基类换成 `XxxService::CallbackService`;方法参数从 `grpc::ServerContext*` 换成 `grpc::CallbackServerContext*`(否则 `DefaultReactor()` 访问权限报错)
- **`grpc::Server::Wait()` 必须放在独立线程**——它和 `io_context::run()` 都是"阻塞式驱动一个事件循环"的调用,两套独立机制不能共享一个线程,也不能把 `Wait()` post 进 `io_context`(会卡住整个 io_context)
- gRPC 不同 service 的 stub **没有公共基类**(各自方法集合不同,无法有意义地做统一多态)。多 service 场景标准做法:**只长期持有 `Channel`,按需现场 `NewStub`**,stub 本身极轻量,没必要预先创建存起来

---

## 八、Boost.MySQL / Boost.Redis 生态坑

- **这类新一代 Boost 网络库是"协程优先"设计的**,纯 `io_context` + 回调/future 路线官方支持相对弱,容易撞上编译错误(`redirect_error` 与 `use_future` 的组合方式、`with_diagnostics` 等工具默认围绕协程举例)
- **`redirect_error` 正确用法是 `redirect_error(底层token, ec)`**,把它当"包一层"而不是当独立参数传;和 `use_future` 搭配意义不大(future 的失败信号本来就是 `.get()` 时抛异常)
- **`with_diagnostics` 是通用 completion token adapter**,不绑定任何特定执行模型,可以配合 `use_awaitable`/`yield_context`/回调任意搭配使用,Boost 1.87 起是 `any_connection` 的默认 token
- **Boost.Redis `GET` 结果必须用 `std::optional<T>`**(而非裸 `T`)——因为 key 不存在时 Redis 协议层返回 nil,裸类型无法承接这种"空"语义,适配会报错
- **`last_insert_id()`**:MySQL 协议 OK_Packet 自带字段,INSERT 时服务端顺带把新生成的自增 ID 一起返回,不需要额外查询;协议层统一用 `uint64_t` 表示,不管实际列类型
- **connection pool 的 "session state"**:临时表、未提交事务、`USE db`、会话变量等跟连接绑定的状态。`INSERT`(涉及 `last_insert_id`)之类会弄脏状态需要 reset,单纯 `SELECT`/`UPDATE`(不触发副作用)可以 `return_without_reset()` 省一次往返
- **三条选择路径,遇到"想要同步阻塞拿结果"的场景时**:
  1. 全面上 C++20 协程(调用链一路跟着改,一次性但改动面最大)
  2. 用有栈协程 `yield_context`(不用升级语言标准,但依然有"传染性",只是传染方式变成传参数)
  3. **如果函数本来语义就是"阻塞等结果",直接用 Boost.MySQL 提供的同步 API**(不带 `async_` 前缀),零 completion token 坑,只需保证调用线程跟"驱动网络 I/O 的 io_context 线程"分开即可——这条路容易被忽略,但常常是最贴合实际需求的选择

---

## 九、调试与压测工具

- **协程/异步代码比回调更难单步调试**——日志优先于 gdb 单步(异步的核心难点是时序,日志天然记录时序);gdb 对 C++20 协程有一定支持但不完美
- **ASan(AddressSanitizer)**:内存安全问题(越界、UAF、double-free),速度快、对第三方库误报远少于 Valgrind,应作为日常首选
- **TSan(ThreadSanitizer)**:专查竞态,能精确报出"哪两处代码无同步访问了同一内存",本项目这几次的丢失唤醒/共享 map 竞态类 bug,TSan 是最对症的工具,和 ASan 不能同时开
- **UBSan**:查未定义行为(整数溢出、空指针解引用等),可以和 ASan 一起开
- **重要边界**:ASan/TSan 查不出"逻辑资源泄漏"(比如忘记调用 `clear_session` 导致 `map` 持续增长)——这类问题本质是"合法持有但业务上不该继续持有",不是内存安全问题,得靠打印资源计数、Massif 堆内存曲线、pprof 堆快照这类手段排查
- **自建的 bench_client**(correctness + bench 两种模式)：正确性用例覆盖边界长度、突发多条消息(专测写队列顺序)、多连接并发(专测竞态)；压测记录 P50/P90/P95/P99 而非只看均值,尾延迟经常比均值更能反映架构差异

---

## 十、C++ 语言细节拾遗

- **`std::thread` 传右值走移动构造,传左值走拷贝构造**(完美转发决定);裸指针/`int`/`shared_ptr` 这类拷贝移动开销相近的类型,传左右值无实质差异,只有大对象(`vector`/`string`/`unique_ptr`)才值得刻意 `std::move`
- **该删移动构造/赋值的场景**:`enable_shared_from_this` 且有异步回调捕获 `self`/`this` 的类;持有 `std::thread` 且线程函数体捕获了 `this` 的类;含 `mutex`/`condition_variable` 成员的类(编译器自动隐式删除,不用手写);向第三方库注册了"自身地址"做回调上下文的类;侵入式容器节点。判断口诀:**只要某处把 `this` 交给了"未来某个时刻才会回调"的地方,这个类就该禁止移动**
- **`boost::system::error_code` 与 `std::error_code`**:同源设计(标准库照抄了 Boost 的思路),API 相似但不是同一类型,不能隐式互转(较新版本支持显式转换)
- **`boost::system::result<T>` / `tl::expected<T,E>` 本质等价,都不同于 `optional<T>`**:optional 只表达"有没有",result/expected 额外携带"失败的具体原因"

---

## 待跟进 / 未解决的疑问

- coroutine_v3(concurrent_channel 版)max 延迟不稳定,偶发 10+ 秒——怀疑方向:`make_shared<MsgNode>` 高频堆分配撞上内存分配器竞争(计划测 jemalloc/tcmalloc)、线程超订、`MsgNode` 定长数组无效复制
- v2(完全隔离多 io_context)在负载不均场景下是否还能保持优势,尚未测试验证
- `MsgNode` 从定长数组改成按需分配(`std::string`/`vector<char>`)之后,重新跑一轮全部版本对比
