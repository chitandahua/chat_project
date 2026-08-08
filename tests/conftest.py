import random
import string
import time

import pytest

import config
import gate_client
import redis_helper
from chat_client import ChatClient


def _rand_suffix(n=8):
    return "".join(random.choices(string.ascii_lowercase + string.digits, k=n))


@pytest.fixture
def register_user():
    """工厂 fixture:调用返回一个已经在数据库里注册成功的用户信息。
    用法: user = register_user()  或者  register_user(user="xxx")
    """

    def _register(user: str = None, passwd: str = "123456"):
        suffix = _rand_suffix()
        user = user or f"test_{suffix}"
        email = f"{suffix}@test.local"

        resp = gate_client.get_verify_code(email)
        assert resp.status_code == 200, resp.text

        # 真实验证码从 redis 直接读,HTTP 返回的 code 字段是假的
        code = redis_helper.get_verify_code(email)
        assert code is not None, f"verify code for {email} not found in redis"

        resp = gate_client.user_register(user, email, passwd, passwd, code)
        body = resp.json()
        assert body.get("status") == 0, f"register failed: {body}"

        return {
            "user": user,
            "email": email,
            "passwd": passwd,
            "id": body["data"]["id"],
        }

    return _register


@pytest.fixture
def login_http():
    """工厂 fixture:HTTP 登录,拿到 token/host/port"""

    def _login(user: str, passwd: str):
        resp = gate_client.user_login(user, passwd)
        body = resp.json()
        assert body.get("status") == 0, f"login failed: {body}"
        return body["data"]  # {host, id, port, token, user}

    return _login


@pytest.fixture
def chat_login():
    """工厂 fixture:建立 TCP 连接并完成登录(消息 id 1005/1006)。
    返回 (client, login_response_body)
    """
    clients = []

    def _chat_login(uid: int, token: str, host: str = None, port: int = None):
        host = host or config.CHAT_SERVER1[0]
        port = port or config.CHAT_SERVER1[1]
        client = ChatClient(host, port)
        clients.append(client)
        client.send(1005, {"uid": uid, "token": token})
        msg_id, body = client.recv()
        assert msg_id == 1006, f"expected login response 1006, got {msg_id}: {body}"
        return client, body

    yield _chat_login

    for c in clients:
        c.close()


@pytest.fixture
def logged_in_user(register_user, login_http, chat_login):
    """一步到位:注册 + HTTP 登录 + TCP 登录,直接给一个可用的 (client, user_info) """

    def _make(user: str = None, passwd: str = "123456"):
        u = register_user(user=user, passwd=passwd)
        login_data = login_http(u["user"], u["passwd"])
        client, login_body = chat_login(
            login_data["id"], login_data["token"], login_data["host"], login_data["port"]
        )
        return client, {**u, **login_data, "login_response": login_body}

    return _make


def chat_server2_available() -> bool:
    """探测 chat_server2 是否可连,用于跨服务器通知类用例的条件跳过"""
    try:
        c = ChatClient(*config.CHAT_SERVER2, timeout=1)
        c.close()
        return True
    except OSError:
        return False


requires_chat_server2 = pytest.mark.skipif(
    not chat_server2_available(), reason="chat_server2 未启动,跳过跨服务器用例"
)


def is_ok(body: dict) -> bool:
    """TCP 响应成功判断:login 用 `status`,其余请求/推送用 `error` 信封。
    成功:status==0 或 error==0;裸 UserInfo(搜索成功)没有这两个字段,视为成功。"""
    if "status" in body:
        return body["status"] == 0
    if "error" in body:
        return body["error"] == 0
    return True  # 无 status/error 字段(如搜索返回的裸 UserInfo)视为成功


def is_error(body: dict) -> bool:
    """TCP 响应失败判断:login 用 `status`,其余用 `error` 信封。"""
    if "status" in body:
        return body["status"] != 0
    if "error" in body:
        return body["error"] != 0
    return False  # 裸对象(无信封)不算错误
