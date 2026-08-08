"""对应 TEST_PLAN.md 第一部分:gate_server HTTP 接口"""

import json

import gate_client
import redis_helper


def gate_status(resp) -> int:
    """解析 gate 响应:成功时 body 是 {"status":0,...};非法输入时服务端返回
    纯文本 "Invalid JSON body"(非 JSON),此时视为失败(非 0)。"""
    try:
        body = resp.json()
    except (json.JSONDecodeError, ValueError):
        return -1  # 非 JSON 响应视为失败
    return body.get("status", -1)


def gate_ok(resp) -> bool:
    return gate_status(resp) == 0


# ---------------------------- 1.1 基础 GET ----------------------------

def test_get_test_basic():
    resp = gate_client.get_test({"key1": "value1", "key2": "value2"})
    assert resp.status_code == 200
    assert "value1" in resp.text
    assert "value2" in resp.text


def test_get_test_no_params():
    resp = gate_client.get_test({})
    assert resp.status_code == 200  # 只要求不崩溃


def test_get_test_special_chars():
    resp = gate_client.get_test({"key1": "a b", "key2": "<script>"})
    assert resp.status_code == 200


# ---------------------------- 1.2 获取验证码 ----------------------------

def test_get_verify_code_success():
    resp = gate_client.get_verify_code("verifycode_test@test.local")
    assert resp.status_code == 200
    code = redis_helper.get_verify_code("verifycode_test@test.local")
    assert code is not None and len(code) > 0


def test_get_verify_code_idempotent_within_ttl():
    """短时间内重复请求同一邮箱,验证码应该保持不变(未过期不应该被覆盖成新值)"""
    email = "verifycode_repeat@test.local"
    gate_client.get_verify_code(email)
    code1 = redis_helper.get_verify_code(email)

    gate_client.get_verify_code(email)
    code2 = redis_helper.get_verify_code(email)

    assert code1 == code2, "同一邮箱短时间内重复请求,验证码不应改变"


def test_get_verify_code_missing_email():
    resp = gate_client.post_raw_json_string("/get_verify_code", "{}")
    # 只要求不是 200+status=0 的"成功"语义,具体错误码由实现决定
    assert not gate_ok(resp), f"缺少 email 时不应该返回成功: {resp.text}"


# ---------------------------- 1.3 用户注册 ----------------------------

def test_register_success(register_user):
    user = register_user()
    assert user["id"] > 0


def test_register_wrong_verify_code():
    email = "reg_wrongcode@test.local"
    gate_client.get_verify_code(email)
    resp = gate_client.user_register("reg_wrongcode_user", email, "123456", "123456", "not-the-real-code")
    body = resp.json()
    assert body.get("status") != 0


def test_register_code_from_other_email():
    """拿 A 邮箱申请的验证码,去注册 B 邮箱,应该失败"""
    email_a = "reg_code_a@test.local"
    email_b = "reg_code_b@test.local"
    gate_client.get_verify_code(email_a)
    code_a = redis_helper.get_verify_code(email_a)

    resp = gate_client.user_register("reg_code_cross_user", email_b, "123456", "123456", code_a)
    body = resp.json()
    assert body.get("status") != 0


def test_register_duplicate_email(register_user):
    user = register_user()
    # 用同一个邮箱再申请一次验证码、再注册一次(用户名换一个,邮箱不变)
    gate_client.get_verify_code(user["email"])
    code = redis_helper.get_verify_code(user["email"])
    resp = gate_client.user_register("different_username_xyz", user["email"], "123456", "123456", code)
    body = resp.json()
    assert body.get("status") != 0, "邮箱已注册,应该拒绝"


def test_register_invalid_json():
    resp = gate_client.post_raw_json_string("/user_register", "{not valid json")
    assert not gate_ok(resp), f"非法 JSON 不应该成功: {resp.text}"


def test_register_missing_fields():
    resp = gate_client.post_raw_json_string("/user_register", '{"email":"only_email@test.local"}')
    assert not gate_ok(resp), f"缺少必填字段不应该成功: {resp.text}"


# ---------------------------- 1.4 重置密码 ----------------------------

def test_reset_password_success(register_user, login_http):
    user = register_user()
    gate_client.get_verify_code(user["email"])
    code = redis_helper.get_verify_code(user["email"])

    new_passwd = "newpass456"
    resp = gate_client.reset_password(user["user"], user["email"], new_passwd, code)
    body = resp.json()
    assert body.get("status") == 0, body

    # 用新密码能登录,证明确实改成功了
    login_data = login_http(user["user"], new_passwd)
    assert login_data["user"] == user["user"]


def test_reset_password_user_email_mismatch(register_user):
    user_a = register_user()
    user_b = register_user()

    gate_client.get_verify_code(user_a["email"])
    code = redis_helper.get_verify_code(user_a["email"])

    # 用 A 的验证码,但填 B 的用户名 + A 的邮箱(用户名和邮箱对不上同一条记录)
    resp = gate_client.reset_password(user_b["user"], user_a["email"], "whatever", code)
    body = resp.json()
    assert body.get("status") != 0


def test_reset_password_nonexistent_user():
    resp = gate_client.reset_password("no_such_user_xyz", "no_such_email_xyz@test.local", "123456", "fakecode")
    body = resp.json()
    assert body.get("status") != 0


# ---------------------------- 1.5 登录获取 token ----------------------------

def test_login_success(register_user, login_http):
    user = register_user()
    login_data = login_http(user["user"], user["passwd"])
    assert login_data["token"]
    assert login_data["host"]
    assert login_data["port"] > 0


def test_login_wrong_password(register_user):
    user = register_user()
    resp = gate_client.user_login(user["user"], "definitely-wrong-password")
    body = resp.json()
    assert body.get("status") != 0


def test_login_nonexistent_user():
    resp = gate_client.user_login("no_such_user_abc", "123456")
    body = resp.json()
    assert body.get("status") != 0
