# chat_project 集成测试

覆盖 `TEST_PLAN.md` 里标了 ✅ 的用例:gate_server HTTP 接口 + chat_server 长连接(登录、搜索、加好友、认证、聊天),包含正常流程和边界/异常场景。

## 准备

```bash
pip install -r requirements.txt
```

启动所有依赖服务(redis、mariadb、verify_server、gate_server、status_server、chat_server1)。
跨服务器场景(标了 `requires_chat_server2` 的用例)需要额外启动 `chat_server2`,没启动会自动跳过,不会报错。

`config.py` 里的端口/地址如果跟你本地不一致,改这一个文件就行。

## 运行

```bash
# 跑全部
pytest -v

# 跳过耗时的超时类用例(比如 10s 登录超时测试),日常开发时更快
pytest -v -m "not slow"

# 只跑某个文件/某个用例
pytest -v test_chat_server.py
pytest -v test_chat_server.py::test_add_friend_success
```

## 已确认事项

- 验证码通过直接读 Redis 获取,不依赖邮件发送,这是预期设计,不需要改
- 测试环境两个 chat_server(18080/18081)都会启动,跨服务器用例正常参与运行,不会被跳过
- `AuthFriendRequest` 确认是 `1013`,以 `message_common.hpp` 为准,`message_ids.py` 已经是最终版本
- 测试数据不做自动清理(用随机后缀邮箱/用户名避免撞库),如果后续需要更干净的重复运行环境,可以再加一个按 `test_` 前缀清库的脚本,目前先按这个方式跑

## 用法提醒

- **好友申请重复、认证不存在的申请**这几个用例断言的是"应该返回非 0 状态码",如果服务端目前对这些场景的处理不是"返回错误"而是别的行为(比如静默忽略),用例会失败——这正是用来验证行为是否符合预期的地方,失败了说明该补这部分逻辑,不代表测试写错了。
