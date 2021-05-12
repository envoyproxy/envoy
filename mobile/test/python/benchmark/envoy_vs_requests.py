# call to construct the Engine before we monkeypatch
# because gevent.monkey.patch_all() patches threading.Event
# which ruins the Engine constructor
# TODO: fix this
from library.python.envoy_requests.common.engine import Engine
Engine.handle()

import gevent.monkey
gevent.monkey.patch_all()

import gevent
import pytest
import requests
from gevent.pool import Group

from library.python.envoy_requests import gevent as envoy_requests


def ping_api(requests_impl, url: str, concurrent_requests: int):
    group = Group()
    for _ in range(concurrent_requests):
        group.spawn(requests_impl.get, url)
    group.join()


@pytest.mark.parametrize(
    "implementation",
    [
        pytest.param(requests, id="requests"),
        pytest.param(requests.Session(), id="requests_session"),
        pytest.param(envoy_requests, id="envoy_requests"),
    ],
)
@pytest.mark.parametrize("concurrent_requests", [1, 10, 100])
def test_performance(benchmark, implementation, concurrent_requests):
    benchmark(ping_api, implementation, "https://www.google.com/", concurrent_requests)
