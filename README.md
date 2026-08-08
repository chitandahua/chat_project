# chat_project

基于 llfc 的 C++ 全栈聊天项目思路重写的即时通讯服务端,采用 C++20 协程(asio awaitable)。

## 架构

分布式聊天系统,按职责拆成多个服务,各自独立进程、通过配置指定地址/端口:

```
                 +------------------+      gRPC       +------------------+
   HTTP 注册/登录  |   gate_server    | <------------> |  status_server    |
 客户端 ---------> |  (HTTP 网关)     |                |  (token/选服务器)  |
                 +------------------+                +------------------+
                          |  gRPC                         |
                          v                              v
                 +------------------+            +------------------+
                 |  verify_server    |            |     Redis        |
                 |  (邮箱验证码)      |            +------------------+
                 +------------------+                   ^
                                                        | 直连
                 +------------------+   gRPC 互连   +------------------+
                 |  chat_server1     | <-----------> |  chat_server2    |
                 |  (TCP 长连接)     |               +------------------+
                 +------------------+
                    |                    +------------------+
                    |                    |     MariaDB      |
                    +------------------->+------------------+
```

| 服务 | 职责 | 监听端口 |
|------|------|---------|
| `gate_server` | HTTP 网关:注册、验证码、重置密码、登录获取 token/chat_server 地址 | 10086 |
| `verify_server` | gRPC 生成邮箱验证码(存 Redis,600s 过期) | 10087 |
| `status_server` | gRPC 分配 token,选择负载最小的 chat_server | 10088 |
| `chat_server1` | TCP 长连接:登录、好友搜索/申请/认证、1:1 文本聊天 | 18080 |
| `chat_server2` | 同上(用于跨服务器通知场景) | 18081 |

所有服务都直连 Redis + MariaDB。

## 依赖

- CMake 3.16+,Ninja
- C++20 编译器(clang++ 优先,否则 gcc)
- Conan 2(拉取第三方库)
- MariaDB/MySQL、Redis

conanfile.txt 声明的依赖:`nlohmann_json`、`tomlplusplus`、`tl-expected`、`magic_enum`。

## 编译

```shell
$ conan install . --output-folder=build # --build=missing
$ cmake --preset conan-release -G Ninja
$ cmake --build --preset conan-release -j$(nproc)
```

产物在 `build/<server>/<server>`,每个服务一个可执行文件。

## 启动

### 1. 前置:数据库与缓存

- 启动 MariaDB/Redis,建库建表(`sql/user.sql`、`sql/friend.sql`,建 `chat_project` 库)
- 配置文件里 MySQL/Redis 的 `user`/`pass` 为空时,本机空密码可直连(见 `config/*`)

### 2. 启动所有服务(每个服务一个终端,或后台运行)

```shell
$ ./build/verify_server/verify_server  ./config/verify_server.json
$ ./build/status_server/status_server  ./config/status_server.json
$ ./build/chat_server/chat_server      ./config/chat_server.json    # chat_server1
$ ./build/chat_server/chat_server      ./config/chat_server2.json   # chat_server2
$ ./build/gate_server/gate_server      ./config/gate_server.toml
```

> 每个服务必须通过命令行参数指定配置文件路径。

## 使用

### 1. 获取验证码

```shell
$ curl -X POST localhost:10086/get_verify_code -H "Content-Type: application/json" \
  -d '{"email":"test@126.com"}'
```

> 当前邮件发送未实现,验证码直接回显在响应里(也存 Redis:`GET <email>`)。

### 2. 注册

```shell
$ curl -X POST localhost:10086/user_register -H "Content-Type: application/json" \
  -d '{"user":"user1","email":"test@126.com","passwd":"123456","confirm_passwd":"123456","verify_code":"<code>"}'
```

### 3. 重置密码

```shell
$ curl -X POST localhost:10086/reset_password -H "Content-Type: application/json" \
  -d '{"user":"user1","email":"test@126.com","passwd":"newpass","verify_code":"<code>"}'
```

### 4. 登录获取 token 与 chat_server 地址

```shell
$ curl -X POST localhost:10086/user_login -H "Content-Type: application/json" \
  -d '{"user":"user1","passwd":"123456"}'
# → {"data":{"id":1,"user":"user1","token":"<uuid>","host":"127.0.0.1","port":18080},...}
```

### 5. TCP 长连接(chat_server)

登录后拿 `host`/`port`/`token`,用 TCP 连 chat_server,协议如下:

```
线上帧格式(大端):
  [id: u32 BE][body_length: u16 BE][body: JSON, 最长 1024 字节]
```

| id | 方向 | 消息 | body 关键字段 |
|----|------|------|--------------|
| 1005 | C→S | 登录请求 | `{"uid","token"}`(建连后 10s 内必须发,否则断开) |
| 1006 | S→C | 登录响应 | `{"status","data":{uid,token,name,friend_list,apply_list}}` |
| 1007 | C→S | 搜索用户 | `{"uid"}` 或 `{"name"}` |
| 1008 | S→C | 搜索响应 | 命中返回裸 UserInfo;失败 `{"error","message"}` |
| 1009 | C→S | 好友申请 | `{"uid","touid"}` |
| 1010 | S→C | 申请响应 | `{"error","message"}` |
| 1011 | S→C | 申请通知(推送) | `{"applyuid","name"}` |
| 1013 | C→S | 认证(同意)好友 | `{"fromuid","touid"}`(fromuid=同意方) |
| 1014 | S→C | 认证响应 | `{"error","message"}` |
| 1015 | S→C | 认证通知(推送) | `{"fromuid","touid"}` |
| 1017 | C→S | 文本聊天 | `{"fromuid","touid","text_array":[{msgid,content}]}` |
| 1018 | S→C | 聊天响应 | `{"error","message"}` |
| 1019 | S→C | 聊天推送 | `{"fromuid","touid","text_array":[...]}` |

说明:

- 错误信封统一为 `{"error":<码>,"message":"<名>"}`,`error==0` 表示成功(登录响应例外,用 `status`)
- 好友列表 + 待处理申请只在登录响应(1006)里返回,没有刷新接口
- 文本聊天是纯转发,无持久化;对端离线时消息直接丢弃(仍返回成功)
- 首个消息必须是登录,否则断开;认证失败也断开

`chat_client/main.cpp` 是一个命令行测试客户端,`chat_client/*.txt` 是它的请求样例。

## 测试

`tests/` 下有 pytest 集成测试套件,覆盖 gate HTTP 接口与 chat_server 长连接(含边界/异常场景)。

```shell
$ cd tests
$ pip install -r requirements.txt
$ pytest            # 全部
$ pytest -m "not slow"   # 跳过耗时的登录超时用例
```

详见 `tests/README.md` 与 `tests/TEST_PLAN.md`。

## 其他文档

- `docs/desc.md`:各服务端点的详细行为说明(curl 示例)
- `sql/`:`user`/`friend`/`friend_apply` 建表语句
- `config/`:各服务的地址/端口/Redis/MySQL 配置
