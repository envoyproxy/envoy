#
# This is pytest plugin providing fixtures for tests.
#

import functools
from contextlib import contextmanager, ExitStack
from typing import Callable, ContextManager, Iterator
from unittest.mock import patch

import pytest


@contextmanager
def nested(*contexts) -> Iterator[tuple]:
    with ExitStack() as stack:
        yield tuple(stack.enter_context(context) for context in contexts)


def _patches(*args: str, prefix: str = "") -> ContextManager[tuple]:
    """Takes a list of module/class paths to patch and an optional prefix

    The prefix is used to prefix all of the paths

    The patches are applied in a nested set of context managers.

    The yields (mocks) are yielded as a tuple.
    """

    patched = []
    prefix = f"{prefix}." if prefix else ""
    for arg in args:
        if isinstance(arg, (list, tuple)):
            path, kwargs = arg
            patched.append(patch(f"{prefix}{path}", **kwargs))
        else:
            patched.append(patch(f"{prefix}{arg}"))
    return nested(*patched)


@pytest.fixture
def patches() -> Callable[..., ContextManager[tuple]]:
    return _patches


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
