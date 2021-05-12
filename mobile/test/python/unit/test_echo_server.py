import requests


# since we already have requests installed for the benchmark
# we reuse it to make sure that the echo server works as expected
# requests provides a basis of correctness against which we can
# measure our implementation


def test_echo_server_get(http_server_url: str):
    response = requests.get(http_server_url).json()
    assert response["body"] == ""
    assert response["method"] == "GET"
    assert response["path"] == "/"


def test_echo_server_body(http_server_url: str):
    response = requests.get(http_server_url, data="hello world").json()
    assert response["body"] == "hello world"
    assert response["method"] == "GET"
    assert response["path"] == "/"
