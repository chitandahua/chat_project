"""
测试专用的 Redis 直连辅助——因为 verify_server 还没实现真正发邮件,
HTTP 接口返回的 code 字段是写死的假值,真正的验证码只存在 Redis 里,
测试直接读 Redis 拿到真值来打通注册/重置密码流程。
"""

import redis

import config

_client = None


def _get_client():
    global _client
    if _client is None:
        _client = redis.Redis(
            host=config.REDIS_HOST,
            port=config.REDIS_PORT,
            password=config.REDIS_PASSWORD,
            decode_responses=True,
        )
    return _client


def get_verify_code(email: str) -> str | None:
    """对应 verify_server 里 SETEX <email> 600 <code> 这个 key 结构"""
    return _get_client().get(email)
