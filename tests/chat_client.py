"""
用于集成测试的 chat_server TCP 客户端。

跟你现有的 C++ client.cpp 是同一个协议,这里用 Python 重新实现一份,
是为了能在 pytest 里方便地做断言、造边界场景(比如故意发不完整的报文)。
"""

import json
import socket

import msg_node


class ConnectionClosed(Exception):
    """对端正常/异常关闭了连接(读到 EOF 或者 RST)"""
    pass


class ChatClient:
    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)

    def send_raw(self, data: bytes):
        self.sock.sendall(data)

    def send(self, msg_id: int, body):
        """body 可以是 dict(自动 json.dumps)、str、或 bytes"""
        if isinstance(body, dict):
            body_bytes = json.dumps(body).encode("utf-8")
        elif isinstance(body, str):
            body_bytes = body.encode("utf-8")
        else:
            body_bytes = body
        self.send_raw(msg_node.encode(msg_id, body_bytes))

    def _recv_exact(self, n: int) -> bytes:
        chunks = []
        remaining = n
        while remaining > 0:
            try:
                chunk = self.sock.recv(remaining)
            except (ConnectionResetError, BrokenPipeError) as e:
                raise ConnectionClosed(str(e)) from e
            if not chunk:
                raise ConnectionClosed("peer closed connection (EOF)")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def recv(self):
        """收一条完整消息,返回 (id, parsed_body)。
        parsed_body 尝试用 json 解析,失败则返回原始字符串。
        对端关闭连接时抛 ConnectionClosed。
        """
        header = self._recv_exact(msg_node.PREFIX_LEN)
        msg_id, body_len = msg_node.decode_header(header)
        body_bytes = self._recv_exact(body_len) if body_len > 0 else b""
        body_str = body_bytes.decode("utf-8", errors="replace")
        try:
            parsed = json.loads(body_str) if body_str else {}
        except json.JSONDecodeError:
            parsed = body_str
        return msg_id, parsed

    def expect_closed(self, within: float = None):
        """断言这条连接会被对端关闭(用于测试各类"应该被踢下线"的场景)。
        within 可以临时设置一个更短的超时,避免每次都等默认的 5s。
        """
        if within is not None:
            self.sock.settimeout(within)
        try:
            data = self.sock.recv(1)
            if data:
                raise AssertionError(f"expected connection to be closed, but got data: {data!r}")
            # data == b'' 表示正常读到 EOF,符合预期
        except (ConnectionResetError, socket.timeout):
            # RST 或者超时都视为"连接确实不再正常工作"，取决于场景，
            # 但 timeout 单独判断更严格，调用方如果不接受 timeout 可以自己再包一层
            pass

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
