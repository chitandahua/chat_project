# config.py
# 端口/地址取自 config/*.json、config/*.toml,配置改了记得同步这里

GATE_BASE_URL = "http://127.0.0.1:10086"

CHAT_SERVER1 = ("127.0.0.1", 18080)
CHAT_SERVER2 = ("127.0.0.1", 18081)

STATUS_SERVER = ("127.0.0.1", 10088)
VERIFY_SERVER = ("127.0.0.1", 10087)

REDIS_HOST = "127.0.0.1"
REDIS_PORT = 6379
REDIS_PASSWORD = None  # 配置文件里 pass 是空字符串,redis-py 传 None 表示不需要认证

MYSQL_HOST = "127.0.0.1"
MYSQL_PORT = 3306
MYSQL_USER = ""     # 按你本地实际配置填
MYSQL_PASS = ""
MYSQL_DB = "chat_project"
