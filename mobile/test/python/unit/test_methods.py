import pytest

from library.python.envoy_requests import asyncio
from library.python.envoy_requests import gevent
from library.python.envoy_requests import threading
from library.python.envoy_requests.response import Response

METHODS = ["DELETE", "GET", "HEAD", "OPTIONS", "PATCH", "POST", "PUT", "TRACE"]


@pytest.mark.parametrize("method", METHODS)
def test_request_gevent(http_server_url: str, method: str):
    response = gevent.request(method, http_server_url)
    assert_valid_response(response)
    if method != "HEAD":
        assert response.json().get("method") == method


@pytest.mark.parametrize("method", METHODS)
def test_method_func_gevent(http_server_url: str, method: str):
    method_func = getattr(gevent, method.lower())
    response = method_func(http_server_url)
    assert_valid_response(response)
    if method != "HEAD":
        assert response.json().get("method") == method


@pytest.mark.parametrize("method", METHODS)
def test_request_threading(http_server_url: str, method: str):
    response = threading.request(method, http_server_url)
    assert_valid_response(response)
    if method != "HEAD":
        assert response.json().get("method") == method


@pytest.mark.parametrize("method", METHODS)
def test_method_func_threading(http_server_url: str, method: str):
    method_func = getattr(threading, method.lower())
    response = method_func(http_server_url)
    assert_valid_response(response)
    if method != "HEAD":
        assert response.json().get("method") == method


@pytest.mark.parametrize("method", METHODS)
@pytest.mark.asyncio
async def test_request_asyncio(http_server_url: str, method: str):
    response = await asyncio.request(method, http_server_url)
    assert_valid_response(response)
    if method != "HEAD":
        assert response.json().get("method") == method


@pytest.mark.parametrize("method", METHODS)
@pytest.mark.asyncio
async def test_method_func_asyncio(http_server_url: str, method: str):
    method_func = getattr(asyncio, method.lower())
    response = await method_func(http_server_url)
    assert_valid_response(response)
    if method != "HEAD":
        assert response.json().get("method") == method


def assert_valid_response(response: Response):
    assert response.envoy_error == None, response.envoy_error.message  # type: ignore
    assert response.status_code == 200
