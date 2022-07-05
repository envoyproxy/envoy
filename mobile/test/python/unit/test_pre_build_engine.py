# This module can't be tested normally
# because it needs the Engine not to exist in the process
# so each of the tests here are marked to be run
# only in a particular instance of the test runner
# and targets are specified in //library/python/...
# for each of these tests
from unittest.mock import patch
from contextlib import contextmanager

import pytest

from library.python.envoy_requests import asyncio as asyncio_envoy_requests
from library.python.envoy_requests import gevent as gevent_envoy_requests
from library.python.envoy_requests import threading as threading_envoy_requests


@contextmanager
def spy_on_build(impl):
    with patch.object(impl.Engine, "build", side_effect=impl.Engine.build) as spied_build:
        yield spied_build



@pytest.mark.standalone
@pytest.mark.asyncio
async def test_pre_build_engine_asyncio():
    with spy_on_build(asyncio_envoy_requests) as spied_build:
        await asyncio_envoy_requests.pre_build_engine()
        assert asyncio_envoy_requests.Engine._handle is not None
        spied_build.assert_called_once()

        await asyncio_envoy_requests.pre_build_engine()
        spied_build.assert_called_once()


@pytest.mark.standalone
def test_pre_build_engine_gevent():
    with spy_on_build(gevent_envoy_requests) as spied_build:
        gevent_envoy_requests.pre_build_engine()
        assert gevent_envoy_requests.Engine._handle is not None
        spied_build.assert_called_once()

        gevent_envoy_requests.pre_build_engine()
        spied_build.assert_called_once()


@pytest.mark.standalone
def test_pre_build_engine_threading():
    with spy_on_build(threading_envoy_requests) as spied_build:
        threading_envoy_requests.pre_build_engine()
        assert threading_envoy_requests.Engine._handle is not None
        spied_build.assert_called_once()

        threading_envoy_requests.pre_build_engine()
        spied_build.assert_called_once()
