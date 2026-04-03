"""Helper routines for normalizing user inputs to the forms expected by
``envoy_engine``.

These functions are copies of the equivalents in the envoy_requests package
but are refactored to stand alone rather than being buried in other
functions.
"""

import json as json_lib
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


def normalize_request(
    method: str,
    url: str,
    json: Any = None,
    data: Data = None,
    headers: Headers = None,
    timeout: Timeout = None,
) -> Tuple[Dict[str, Union[str, List[str]]], bytes]:
    """Normalize HTTP request parameters into Envoy-compatible format.

    Args:
        method: HTTP method (e.g., "GET", "POST").
        url: Request URL.
        json: Request JSON body (optional). If provided, it will be serialized to JSON and the content-type header will be set to application/json.
        data: Request body data (optional). If `data` is a dict or list, it will be form-encoded and the content-type header will be set to application/x-www-form-urlencoded. If `data` is a string, it will be encoded as UTF-8 bytes. Only one of `json` or `data` can be provided.
        headers: Request headers dict (optional).
        timeout: Request timeout (optional).

    Returns:
        A tuple of (normalized_headers_dict, request_body_bytes).
        The headers dict contains pseudo-headers (:method, :scheme, :authority, :path)
        and user-provided headers. The body is the request data as bytes.
    """
    # normalize pieces that go into the header map
    norm_method = normalize_method(method)
    if json is not None:
        if data is not None:
            raise ValueError("Only one of 'data' or 'json' can be supplied.")
        norm_data = json_lib.dumps(json).encode("utf-8")
        data_headers = {
            "content-type": ["application/json"],
            "content-length": [str(len(norm_data))],
        }
    else:
        norm_data, data_headers = normalize_data(data)
    norm_headers = {**data_headers, **normalize_headers(headers)}
    norm_timeout_ms = normalize_timeout_to_ms(timeout)

    parsed = urlparse(url)
    header_dict: Dict[str, Union[str, List[str]]] = {
        ":method": norm_method,
        ":scheme": parsed.scheme,
        ":authority": parsed.netloc,
        ":path": parsed.path,
    }
    if norm_timeout_ms > 0:
        header_dict["x-envoy-upstream-rq-timeout-ms"] = str(norm_timeout_ms)
    for key, values in norm_headers.items():
        header_dict[key] = values if len(values) > 1 else values[0]

    return header_dict, norm_data
