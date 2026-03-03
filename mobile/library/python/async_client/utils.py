"""Helper routines for normalizing user inputs to the forms expected by
``envoy_engine``.

These functions are copies of the equivalents in the envoy_requests package
but are refactored to stand alone rather than being buried in other
functions.
"""

from datetime import timedelta
from typing import Any, Dict, List, Optional, Tuple, Union
from urllib.parse import urlencode, urlparse


Data = Optional[Union[bytes, str, Dict[str, Any], List[Tuple[str, Any]]]]
NormalData = bytes

Headers = Optional[Dict[str, Union[str, List[str]]]]
NormalHeaders = Dict[str, List[str]]

Timeout = Optional[Union[int, float, timedelta]]
NormalTimeout = int


def normalize_method(method: str) -> str:
    # envoy-python layers underneath only care about canonical string values
    # for the ":method" header; the C++ shim will accept a plain dict.
    return method.upper()


def normalize_data(data: Data) -> Tuple[NormalData, NormalHeaders]:
    data_headers: NormalHeaders = {}
    byte_data: Optional[bytes] = None

    if isinstance(data, str):
        byte_data = bytes(data, "utf8")
        data_headers["charset"] = ["utf8"]
    elif isinstance(data, dict) or isinstance(data, list):
        byte_data = bytes(urlencode(data), "utf8")
        data_headers["charset"] = ["utf8"]
        data_headers["content-type"] = ["application/x-www-form-urlencoded"]
    elif data is None:
        byte_data = b""
    else:
        byte_data = data  # assume already bytes

    if byte_data is not None:
        data_headers["content-length"] = [str(len(byte_data))]
    return byte_data or b"", data_headers


def normalize_headers(headers: Headers) -> NormalHeaders:
    if headers is None:
        return {}
    normalized: NormalHeaders = {}
    for key, value in headers.items():
        normalized[key] = [value] if isinstance(value, str) else value
    return normalized


def normalize_timeout_to_ms(timeout: Timeout) -> NormalTimeout:
    if timeout is None:
        return 0
    elif isinstance(timeout, int):
        return 1000 * timeout
    elif isinstance(timeout, float):
        return int(1000 * timeout)
    elif isinstance(timeout, timedelta):
        return int(1000 * timeout.total_seconds())
    raise TypeError("unsupported timeout type")
