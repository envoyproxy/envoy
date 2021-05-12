import pytest

from library.python import envoy_requests


def test_send_request(http_server_url: str):
    response = envoy_requests.get(http_server_url, timeout=0.25)
    json = response.json()
    assert json.get("body") == ""
    assert json.get("method") == "GET"
    assert json.get("path") == "/"


def test_send_headers(http_server_url: str):
    response = envoy_requests.get(
        http_server_url,
        headers={"random-header": "random-value"},
        timeout=0.25,
    )
    json = response.json()
    assert json.get("body") == ""
    assert json.get("method") == "GET"
    assert json.get("path") == "/"
    assert json.get("headers").get("random-header") == "random-value"


def test_send_data_bytes(http_server_url: str):
    response = envoy_requests.post(
        http_server_url,
        data=b"hello world",
        timeout=0.25,
    )
    json = response.json()
    assert json.get("body") == "hello world"
    assert json.get("method") == "POST"
    assert json.get("path") == "/"
    assert json.get("headers", {}).get("content-type") == None
    assert json.get("headers", {}).get("charset") == None


def test_send_data_str(http_server_url: str):
    response = envoy_requests.post(
        http_server_url,
        data="hello world",
        timeout=0.25,
    )
    json = response.json()
    assert json.get("body") == "hello world"
    assert json.get("method") == "POST"
    assert json.get("path") == "/"
    assert json.get("headers", {}).get("content-type") == None
    assert json.get("headers", {}).get("charset") == "utf8"


@pytest.mark.parametrize(
    "data", [{"hello": "world encoding"}, [("hello", "world encoding")]]
)
def test_send_data_form_urlencoded(http_server_url: str, data):
    response = envoy_requests.post(
        http_server_url,
        data=data,
        timeout=0.25,
    )
    json = response.json()
    assert json.get("body") == "hello=world+encoding"
    assert json.get("method") == "POST"
    assert json.get("path") == "/"
    assert (
        json.get("headers", {}).get("content-type")
        == "application/x-www-form-urlencoded"
    )
    assert json.get("headers", {}).get("charset") == "utf8"
