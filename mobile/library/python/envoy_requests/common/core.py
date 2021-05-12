from datetime import timedelta
from typing import Any
from typing import Callable
from typing import Dict
from typing import List
from typing import Optional
from typing import Tuple
from typing import Union
from urllib.parse import urlencode
from urllib.parse import urlparse

import envoy_engine

from ..response import Response
from .executor import Executor


Data = Optional[Union[bytes, str, Dict[str, Any], List[Tuple[str, Any]]]]
NormalData = bytes

Headers = Optional[Dict[str, Union[str, List[str]]]]
NormalHeaders = Dict[str, List[str]]

Timeout = Optional[Union[int, float, timedelta]]
NormalTimeout = int


def make_stream(
    engine: envoy_engine.Engine,
    executor: Executor,
    response: Response,
    set_stream_complete: Callable[[], None],
):
    def _on_headers(headers: envoy_engine.ResponseHeaders, _: bool):
        response.status_code = headers.http_status()
        for key in headers:
            value = headers[key]
            response.headers[key] = value[0] if len(value) == 1 else value

    def _on_trailers(trailers: envoy_engine.ResponseTrailers):
        for key in trailers:
            value = trailers[key]
            response.trailers[key] = value[0] if len(value) == 1 else value

    def _on_data(data: bytes, _: bool):
        response.body_raw.extend(data)

    def _on_complete():
        set_stream_complete()

    def _on_error(error: envoy_engine.EnvoyError):
        response.envoy_error = error
        set_stream_complete()

    def _on_cancel():
        set_stream_complete()

    return (
        engine.stream_client()
        .new_stream_prototype()
        .set_on_headers(executor.wrap(_on_headers))
        .set_on_data(executor.wrap(_on_data))
        .set_on_trailers(executor.wrap(_on_trailers))
        .set_on_complete(executor.wrap(_on_complete))
        .set_on_error(executor.wrap(_on_error))
        .set_on_cancel(executor.wrap(_on_cancel))
        .start()
    )


def send_request(
    stream: envoy_engine.Stream,
    method: str,
    url: str,
    *,
    # TODO: support taking JSON data like requests lib(?)
    # i'm not actually sure if i like that design, but think more
    data: Data = None,
    headers: Headers = None,
    timeout: Timeout = None,
):
    norm_method = normalize_method(method)
    norm_data, data_headers = normalize_data(data)
    norm_headers = {**data_headers, **normalize_headers(headers)}
    norm_timeout_ms = normalize_timeout_to_ms(timeout)

    structured_url = urlparse(url)
    request_headers_builder = envoy_engine.RequestHeadersBuilder(
        norm_method,
        structured_url.scheme,
        structured_url.netloc,
        structured_url.path,
    )
    if norm_timeout_ms > 0:
        request_headers_builder.add(
            "x-envoy-upstream-rq-timeout-ms", str(norm_timeout_ms)
        )
    for key, values in norm_headers.items():
        for value in values:
            request_headers_builder.add(key, value)
    request_headers = request_headers_builder.build()

    has_data = len(norm_data) > 0
    stream.send_headers(request_headers, not has_data)
    if has_data:
        stream.close(norm_data)


def normalize_method(method: str) -> envoy_engine.RequestMethod:
    return {
        "DELETE": envoy_engine.RequestMethod.DELETE,
        "GET": envoy_engine.RequestMethod.GET,
        "HEAD": envoy_engine.RequestMethod.HEAD,
        "OPTIONS": envoy_engine.RequestMethod.OPTIONS,
        "PATCH": envoy_engine.RequestMethod.PATCH,
        "POST": envoy_engine.RequestMethod.POST,
        "PUT": envoy_engine.RequestMethod.PUT,
        "TRACE": envoy_engine.RequestMethod.TRACE,
    }[method.upper()]


def normalize_data(data: Data) -> Tuple[NormalData, NormalHeaders]:
    # TODO: take existing headers, so that we can:
    #   - prefer charset when provided
    #   - encode data in user's content type when provided
    #   - probably more!
    data_headers = {}

    if isinstance(data, str):
        byte_data = bytes(data, "utf8")
        data_headers["charset"] = ["utf8"]
    elif isinstance(data, dict):
        byte_data = bytes(
            urlencode([(key, str(value)) for key, value in data.items()]),
            "utf8",
        )
        data_headers["charset"] = ["utf8"]
        data_headers["content-type"] = ["application/x-www-form-urlencoded"]
    elif isinstance(data, list):
        byte_data = bytes(
            urlencode([(key, str(value)) for key, value in data]),
            "utf8",
        )
        data_headers["charset"] = ["utf8"]
        data_headers["content-type"] = ["application/x-www-form-urlencoded"]
    elif data is None:
        byte_data = b""
    else:
        byte_data = data

    if byte_data is not None:
        data_headers["content-length"] = [str(len(byte_data))]
    return byte_data, data_headers


def normalize_headers(headers: Headers) -> NormalHeaders:
    if headers is None:
        return {}
    normalized_headers = {}
    for key in headers:
        value = headers[key]
        normalized_headers[key] = [value] if isinstance(value, str) else value
    return normalized_headers


def normalize_timeout_to_ms(timeout: Timeout) -> NormalTimeout:
    if timeout is None:
        return 0
    elif isinstance(timeout, int):
        return 1000 * timeout
    elif isinstance(timeout, float):
        return int(1000 * timeout)
    elif isinstance(timeout, timedelta):
        return int(1000 * timeout.total_seconds())
    else:
        raise TypeError()
