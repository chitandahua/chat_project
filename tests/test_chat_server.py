"""对应 TEST_PLAN.md 第二部分:chat_server 长连接(MsgNode 协议)"""

import time

import pytest

import config
import message_ids as mid
from chat_client import ChatClient, ConnectionClosed
from conftest import is_error, is_ok, requires_chat_server2


# ---------------------------- 2.1 连接与登录 ----------------------------

def test_login_success(logged_in_user):
    client, info = logged_in_user()
    assert info["login_response"]["data"]["name"] == info["user"]


def test_login_wrong_token(register_user, login_http):
    user = register_user()
    login_data = login_http(user["user"], user["passwd"])

    client = ChatClient(login_data["host"], login_data["port"])
    client.send(mid.LOGIN_REQUEST, {"uid": login_data["id"], "token": "this-is-not-the-real-token"})
    # 服务端先回一个错误帧(1006 + TokenInvalid),再断开连接
    msg_id, body = client.recv()
    assert msg_id == mid.LOGIN_RESPONSE
    assert not is_ok(body), f"wrong token 应该被拒绝: {body}"
    client.expect_closed(within=3)
    client.close()


def test_first_message_must_be_login():
    client = ChatClient(*config.CHAT_SERVER1)
    client.send(mid.SEARCH_USER_REQUEST, {"uid": 1})
    # 服务端先回一个错误帧(NotAuthenticated),id 与请求相同,再断开连接
    msg_id, body = client.recv()
    assert msg_id == mid.SEARCH_USER_REQUEST, f"首条非登录消息应被拒绝,收到 id={msg_id}: {body}"
    assert not is_ok(body), f"首条消息必须是登录: {body}"
    client.expect_closed(within=3)
    client.close()


@pytest.mark.slow
def test_login_timeout():
    """建连后什么都不发,10s 后应该被服务端主动断开。耗时较长,标记为 slow。"""
    client = ChatClient(*config.CHAT_SERVER1, timeout=15)
    client.expect_closed(within=12)
    client.close()


# ---------------------------- 2.2 好友搜索 ----------------------------

def test_search_user_by_uid(logged_in_user):
    client, info = logged_in_user()
    client.send(mid.SEARCH_USER_REQUEST, {"uid": info["id"]})
    msg_id, body = client.recv()
    assert msg_id == mid.SEARCH_USER_RESPONSE
    assert body["name"] == info["user"]


def test_search_user_by_name(logged_in_user):
    client, info = logged_in_user()
    client.send(mid.SEARCH_USER_REQUEST, {"name": info["user"]})
    msg_id, body = client.recv()
    assert msg_id == mid.SEARCH_USER_RESPONSE
    assert body["name"] == info["user"]


def test_search_user_not_found(logged_in_user):
    client, _ = logged_in_user()
    client.send(mid.SEARCH_USER_REQUEST, {"uid": 999999999})
    msg_id, body = client.recv()
    assert msg_id == mid.SEARCH_USER_RESPONSE
    assert not is_ok(body)


def test_search_user_empty_request(logged_in_user):
    client, _ = logged_in_user()
    client.send(mid.SEARCH_USER_REQUEST, {})
    msg_id, body = client.recv()
    assert msg_id == mid.SEARCH_USER_RESPONSE
    assert not is_ok(body)


# ---------------------------- 2.3 好友申请 ----------------------------

def test_add_friend_success(logged_in_user):
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    client_a.send(mid.ADD_FRIEND_REQUEST, {"uid": info_a["id"], "touid": info_b["id"]})
    msg_id, body = client_a.recv()
    assert msg_id == mid.ADD_FRIEND_RESPONSE
    assert is_ok(body)

    # B 应该收到通知
    notify_id, notify_body = client_b.recv()
    assert notify_id == mid.NOTIFY_ADD_FRIEND
    assert notify_body["applyuid"] == info_a["id"]
    assert notify_body["name"] == info_a["user"]


def test_add_friend_to_self(logged_in_user):
    client, info = logged_in_user()
    client.send(mid.ADD_FRIEND_REQUEST, {"uid": info["id"], "touid": info["id"]})
    msg_id, body = client.recv()
    assert msg_id == mid.ADD_FRIEND_RESPONSE
    assert not is_ok(body), "申请自己应该被拒绝"


def test_add_friend_impersonate_uid(logged_in_user):
    """消息里的 uid 字段跟这条连接实际登录的 uid 不符,应该被拒绝(防止冒充)"""
    client_a, info_a = logged_in_user()
    _, info_b = logged_in_user()

    fake_uid = info_a["id"] + 1  # 随便一个不是 info_a 自己的 uid
    client_a.send(mid.ADD_FRIEND_REQUEST, {"uid": fake_uid, "touid": info_b["id"]})
    msg_id, body = client_a.recv()
    assert msg_id == mid.ADD_FRIEND_RESPONSE
    assert not is_ok(body), "uid 与登录身份不符,应该被拒绝"


def test_add_friend_target_offline(logged_in_user, register_user):
    """touid 对应的用户当前没有连接任何 chat_server,请求应该成功但不产生通知,不报错"""
    client_a, info_a = logged_in_user()
    offline_user = register_user()  # 只注册,不登录,保证是离线状态

    client_a.send(mid.ADD_FRIEND_REQUEST, {"uid": info_a["id"], "touid": offline_user["id"]})
    msg_id, body = client_a.recv()
    assert msg_id == mid.ADD_FRIEND_RESPONSE
    assert is_ok(body), "对方离线不应该导致申请本身失败"


def test_add_friend_duplicate(logged_in_user):
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    client_a.send(mid.ADD_FRIEND_REQUEST, {"uid": info_a["id"], "touid": info_b["id"]})
    client_a.recv()
    client_b.recv()  # 消费掉第一次的 NotifyAddFriend

    # 重复申请同一对 (from, to)
    client_a.send(mid.ADD_FRIEND_REQUEST, {"uid": info_a["id"], "touid": info_b["id"]})
    msg_id, body = client_a.recv()
    assert msg_id == mid.ADD_FRIEND_RESPONSE
    assert not is_ok(body), "重复申请应该被数据库唯一约束拒绝,而不是静默成功"


@requires_chat_server2
def test_add_friend_cross_server(logged_in_user):
    client_a, info_a = logged_in_user()  # 默认连 chat_server1
    client_b, info_b = logged_in_user()
    # 注:如果 logged_in_user 内部走的是 HTTP 登录分配到的 server,两个用户拿到的
    # server 分配可能不确定落在 1 还是 2 上;如果你的 status_server 分配策略是轮询,
    # 多注册几个用户大概率能覆盖到跨服务器场景,这里先按"能连上就测"处理。
    client_a.send(mid.ADD_FRIEND_REQUEST, {"uid": info_a["id"], "touid": info_b["id"]})
    msg_id, body = client_a.recv()
    assert msg_id == mid.ADD_FRIEND_RESPONSE
    assert is_ok(body)


# ---------------------------- 2.4 好友认证 ----------------------------

def test_auth_friend_success(logged_in_user):
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    client_a.send(mid.ADD_FRIEND_REQUEST, {"uid": info_a["id"], "touid": info_b["id"]})
    client_a.recv()
    client_b.recv()  # NotifyAddFriend

    client_b.send(mid.AUTH_FRIEND_REQUEST, {"fromuid": info_b["id"], "touid": info_a["id"]})
    msg_id, body = client_b.recv()
    assert msg_id == mid.AUTH_FRIEND_RESPONSE
    assert is_ok(body)

    notify_id, notify_body = client_a.recv()
    assert notify_id == mid.NOTIFY_AUTH_FRIEND


def test_auth_friend_nonexistent_apply(logged_in_user):
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    # 没有任何 add_friend 申请,直接认证
    client_b.send(mid.AUTH_FRIEND_REQUEST, {"fromuid": info_b["id"], "touid": info_a["id"]})
    msg_id, body = client_b.recv()
    assert msg_id == mid.AUTH_FRIEND_RESPONSE
    assert not is_ok(body)


def test_auth_friend_uid_mismatch(logged_in_user):
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    client_a.send(mid.ADD_FRIEND_REQUEST, {"uid": info_a["id"], "touid": info_b["id"]})
    client_a.recv()
    client_b.recv()

    # 用 A 的连接,却假装是 B 在认证(fromuid 填 B,但这条连接登录的是 A)
    client_a.send(mid.AUTH_FRIEND_REQUEST, {"fromuid": info_b["id"], "touid": info_a["id"]})
    msg_id, body = client_a.recv()
    assert msg_id == mid.AUTH_FRIEND_RESPONSE
    assert not is_ok(body), "fromuid 与登录身份不符,应该被拒绝"


# ---------------------------- 2.5 文本聊天 ----------------------------

def test_text_chat_success(logged_in_user):
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    client_a.send(
        mid.TEXT_CHAT_MSG_REQ,
        {"fromuid": info_a["id"], "touid": info_b["id"], "text_array": [{"msgid": 1, "content": "hello"}]},
    )
    msg_id, body = client_a.recv()
    assert msg_id == mid.TEXT_CHAT_MSG_RSP
    assert body.get("error", 0) == 0

    notify_id, notify_body = client_b.recv()
    assert notify_id == mid.NOTIFY_TEXT_CHAT_MSG
    assert notify_body["text_array"][0]["content"] == "hello"


def test_text_chat_target_offline(logged_in_user, register_user):
    client_a, info_a = logged_in_user()
    offline_user = register_user()

    client_a.send(
        mid.TEXT_CHAT_MSG_REQ,
        {"fromuid": info_a["id"], "touid": offline_user["id"], "text_array": [{"msgid": 1, "content": "hi"}]},
    )
    msg_id, body = client_a.recv()
    assert msg_id == mid.TEXT_CHAT_MSG_RSP
    assert body.get("error", 0) == 0, "对方离线时聊天请求本身应该仍然成功(当前无持久化)"


def test_text_chat_empty_array(logged_in_user):
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    client_a.send(mid.TEXT_CHAT_MSG_REQ, {"fromuid": info_a["id"], "touid": info_b["id"], "text_array": []})
    msg_id, body = client_a.recv()
    assert msg_id == mid.TEXT_CHAT_MSG_RSP  # 只要求不崩溃、能收到正常响应


def test_text_chat_max_body_size(logged_in_user):
    """凑一条接近 MAX_LENGTH(1024)的消息,验证边界大小能正常处理"""
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    # 预留一些给 JSON 结构本身的开销,内容部分填充到总体接近上限
    long_content = "x" * 900
    client_a.send(
        mid.TEXT_CHAT_MSG_REQ,
        {"fromuid": info_a["id"], "touid": info_b["id"], "text_array": [{"msgid": 1, "content": long_content}]},
    )
    msg_id, body = client_a.recv()
    assert msg_id == mid.TEXT_CHAT_MSG_RSP
    assert body.get("error", 0) == 0


def test_text_chat_burst_multiple_messages(logged_in_user):
    """连续快速发送多条消息,验证顺序和内容不会错乱——项目开发过程中命中过这类并发 bug"""
    client_a, info_a = logged_in_user()
    client_b, info_b = logged_in_user()

    n = 20
    for i in range(n):
        client_a.send(
            mid.TEXT_CHAT_MSG_REQ,
            {"fromuid": info_a["id"], "touid": info_b["id"], "text_array": [{"msgid": i, "content": f"msg-{i}"}]},
        )

    for i in range(n):
        msg_id, body = client_a.recv()
        assert msg_id == mid.TEXT_CHAT_MSG_RSP

    for i in range(n):
        notify_id, notify_body = client_b.recv()
        assert notify_id == mid.NOTIFY_TEXT_CHAT_MSG
        assert notify_body["text_array"][0]["content"] == f"msg-{i}", (
            f"第 {i} 条消息内容或顺序错乱: {notify_body}"
        )


# ---------------------------- 2.6 协议健壮性 ----------------------------

def test_invalid_json_body_does_not_crash_connection(logged_in_user):
    client, info = logged_in_user()
    client.send(mid.SEARCH_USER_REQUEST, "this is not valid json")
    msg_id, body = client.recv()
    # 只要求收到某种错误响应,且连接接下来还能正常用
    client.send(mid.SEARCH_USER_REQUEST, {"uid": info["id"]})
    msg_id2, body2 = client.recv()
    assert msg_id2 == mid.SEARCH_USER_RESPONSE, "前一条非法请求不应该影响后续消息的正常处理"
