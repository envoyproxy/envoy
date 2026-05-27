"""Utilities for mapping httpx requests and responses to Envoy Mobile."""

from typing import Dict, List, Optional, Union
from urllib.parse import urlparse
import httpx
from .async_client.utils import (
    normalize_method,
    normalize_headers,
    normalize_timeout_to_ms,
)


def get_envoy_headers(
    request: httpx.Request,
    timeout: Optional[Union[int, float]] = None,
) -> Dict[str, Union[str, List[str]]]:
    """Map an httpx.Request to Envoy-compatible headers.

    Args:
        request: The httpx.Request object.
        timeout: Optional request timeout in seconds.

    Returns:
        A dictionary of Envoy-compatible headers, including pseudo-headers.
    """
    method = normalize_method(request.method)
    url = str(request.url)
    parsed = urlparse(url)

    # Convert httpx.Headers to the dict format expected by normalize_headers
    # httpx.Headers.items() provides (key, value) pairs.
    headers_dict: Dict[str, str] = dict(request.headers.items())
    norm_headers = normalize_headers(headers_dict)

    header_dict: Dict[str, Union[str, List[str]]] = {
        ":method": method,
        ":scheme": request.url.scheme,
        ":authority": request.url.netloc.decode("ascii"),
        ":path": request.url.raw_path.decode("ascii"),
    }

    # Add timeout header if specified
    timeout_ms = normalize_timeout_to_ms(timeout)
    if timeout_ms > 0:
        header_dict["x-envoy-upstream-rq-timeout-ms"] = str(timeout_ms)

    # Add normalized user headers
    for key, values in norm_headers.items():
        # Avoid overriding pseudo-headers
        if key.startswith(":"):
            continue
        header_dict[key] = values if len(values) > 1 else values[0]

    return header_dict


def map_envoy_error(error_code: int, message: str) -> Exception:
    """Map an Envoy error code to an appropriate httpx exception."""
    # Envoy error codes are defined in library/python/module_definition.cc
    # ENVOY_STREAM_RESET = 1
    # ENVOY_CONNECTION_FAILURE = 2
    # ENVOY_BUFFER_LIMIT_EXCEEDED = 3
    # ENVOY_REQUEST_TIMEOUT = 4

    if error_code == 2:  # ConnectionFailure
        return httpx.ConnectError(message)
    elif error_code == 4:  # RequestTimeout
        return httpx.ReadTimeout(message)
    elif error_code == 1:  # StreamReset
        return httpx.RemoteProtocolError(message)

    return httpx.RequestError(message)
