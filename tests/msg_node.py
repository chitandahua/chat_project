"""
MsgNode 协议编解码,对应 C++ 那份 msg_node.hpp:

    线上格式: [id: 4字节 BE][body_length: 2字节 BE][body: 变长, 最长 MAX_LENGTH]

x86_64 是小端序,但 C++ 服务端 msg_node.hpp 明确用 boost::endian 转成大端
(native_to_big),所以线上帧是大端。之前这里误写成了小端 '<',导致测试客户端
发出去的帧 id/长度全反,服务端按大端解析直接断连。这里必须用大端 '>IH'。
"""

import struct

ID_LEN = 4
HEAD_LEN = 2
PREFIX_LEN = ID_LEN + HEAD_LEN
MAX_LENGTH = 1024

_HEADER_FMT = ">IH"  # 大端: uint32_t id, uint16_t body_length


class ProtocolError(Exception):
    pass


def encode(msg_id: int, body: bytes) -> bytes:
    if len(body) > MAX_LENGTH:
        raise ProtocolError(f"body too large: {len(body)} > {MAX_LENGTH}")
    header = struct.pack(_HEADER_FMT, msg_id, len(body))
    return header + body


def decode_header(header_bytes: bytes) -> tuple[int, int]:
    """返回 (id, body_length)"""
    if len(header_bytes) != PREFIX_LEN:
        raise ProtocolError(f"header must be {PREFIX_LEN} bytes, got {len(header_bytes)}")
    msg_id, body_length = struct.unpack(_HEADER_FMT, header_bytes)
    if body_length > MAX_LENGTH:
        raise ProtocolError(f"declared body_length too large: {body_length}")
    return msg_id, body_length
