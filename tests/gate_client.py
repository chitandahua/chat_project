"""gate_server HTTP 接口的简单封装,对应 desc.md 里的几个 curl 示例。"""

import requests

import config


def get_test(params: dict) -> requests.Response:
    return requests.get(f"{config.GATE_BASE_URL}/get_test", params=params, timeout=5)


def get_verify_code(email: str) -> requests.Response:
    return requests.post(
        f"{config.GATE_BASE_URL}/get_verify_code", json={"email": email}, timeout=5
    )


def user_register(user: str, email: str, passwd: str, confirm_passwd: str, verify_code: str) -> requests.Response:
    return requests.post(
        f"{config.GATE_BASE_URL}/user_register",
        json={
            "user": user,
            "email": email,
            "passwd": passwd,
            "confirm_passwd": confirm_passwd,
            "verify_code": verify_code,
        },
        timeout=5,
    )


def reset_password(user: str, email: str, passwd: str, verify_code: str) -> requests.Response:
    return requests.post(
        f"{config.GATE_BASE_URL}/reset_password",
        json={"user": user, "email": email, "passwd": passwd, "verify_code": verify_code},
        timeout=5,
    )


def user_login(user: str, passwd: str) -> requests.Response:
    return requests.post(
        f"{config.GATE_BASE_URL}/user_login",
        json={"user": user, "passwd": passwd},
        timeout=5,
    )


def post_raw_json_string(path: str, raw_body: str) -> requests.Response:
    """故意发送非法 JSON body 用的辅助函数(requests 的 json= 参数会自动帮你序列化成合法 JSON,
    测"非法 JSON"场景必须绕开这层,直接传原始字符串当 body)"""
    return requests.post(
        f"{config.GATE_BASE_URL}{path}",
        data=raw_body,
        headers={"Content-Type": "application/json"},
        timeout=5,
    )
