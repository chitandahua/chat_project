# chat_client

基于 MsgNode 协议的 **手动测试客户端**(C++/boost::asio),用于向 `chat_server` 发送/接收消息,验证协议与交互流程。

## 用法

```shell
$ ./client --host=127.0.0.1 --port=18080                # 交互模式,逐行输入
$ ./client --host=127.0.0.1 --port=18080 --file=login.txt  # 从文件逐行读,自动退出
```

- 交互模式下输入 `exit` 退出
- 每行格式:`<id>|<json内容>`,分隔符取**第一个** `|`,后面整体作为 body

## 编译

`main.cpp` 需要 boost::asio 与 `chat_server/msg_node.hpp`。当前 `client` 二进制是直接编译的(未接入顶层 CMake):

```shell
$ g++ -std=c++20 -I<boost_include> main.cpp -o client -lpthread
```

## 完整交互流程

一个典型的手动测试会话(以 aaa/bbb 为例):

```
1. 登录取 token(HTTP,gate_server):
   curl -X POST localhost:10086/user_login -H "Content-Type: application/json" \
     -d '{"user":"aaa","passwd":"22"}'
   # → {"data":{"id":1,"token":"<uuid>","host":"127.0.0.1","port":18080},...}

2. TCP 连 chat_server,登录:
   ./client --host=127.0.0.1 --port=18080 --file=login.txt
   # 1005|{"uid":1,"token":"<uuid>"}  →  1006 登录响应(含 friend_list/apply_list)

3. 搜索用户 / 加好友 / 认证 / 聊天(见下方请求样例)
```

## 请求样例(txt 文件)

| 文件 | 消息 | 说明 |
|------|------|------|
| `login.txt` | 1005 | 登录,`{"uid","token"}`(token 需先经 gate HTTP 登录获取) |
| `search_user.txt` | 1007 | 搜索用户,`{"uid"}` 或 `{"name"}` |
| `add_friend.txt` | 1009 | 好友申请,`{"uid","touid"}` |
| `friend_auth.txt` | 1013 | 认证(同意)好友,`{"fromuid","touid"}`(fromuid=同意方) |

> `login.txt` 里的 token 是**一次性**的,需每次先跑 HTTP 登录换取再替换。
> 想发文本聊天,直接在交互模式输入:`1017|{"fromuid":1,"touid":3,"text_array":[{"msgid":1,"content":"hi"}]}`

## 协议速览

```
线上帧格式(大端): [id: u32 BE][body_length: u16 BE][body: JSON, 最长 1024]
```

- 建连后 10s 内必须发登录(1005),否则服务端断开
- 首条消息必须是登录,否则断开
- 错误响应信封:`{"error":<码>,"message":"<名>"}`(`error==0` 表示成功;登录响应例外,用 `status`)
- 消息 id 对照见顶层 `README.md` / `chat_server/message_common.hpp`

更多协议细节与服务器行为见顶层 `README.md` 与 `docs/desc.md`。
