#
# This is pytest plugin providing fixtures for tests.
#

import functools
from typing import Callable

import pytest


def _async_command_main(patches, main: Callable, handler: str, args: tuple) -> None:
    parts = handler.split(".")
    patched = patches("asyncio.run", parts.pop(), prefix=".".join(parts))

    with patched as (m_run, m_handler):
        assert main(*args) == m_run.return_value

    assert list(m_run.call_args) == [(m_handler.return_value.run.return_value,), {}]
    assert list(m_handler.call_args) == [args, {}]
    assert list(m_handler.return_value.run.call_args) == [(), {}]


def _command_main(
        patches,
        main: Callable,
        handler: str,
        args=("arg0", "arg1", "arg2"),
        async_run: bool = False) -> None:
    if async_run:
        return _async_command_main(patches, main, handler, args=args)

    patched = patches(handler)

    with patched as (m_handler,):
        assert main(*args) == m_handler.return_value.run.return_value

    assert list(m_handler.call_args) == [args, {}]
    assert list(m_handler.return_value.run.call_args) == [(), {}]


@pytest.fixture
def command_main(patches) -> Callable:
    return functools.partial(_command_main, patches)
