#
# This is pytest plugin providing fixtures for tests.
#

import functools
from typing import Callable

import pytest

import nest_asyncio

nest_asyncio.apply()


def _command_main(patches, main: Callable, handler: str, args=("arg0", "arg1", "arg2")) -> None:
    parts = handler.split(".")
    patched = patches(parts.pop(), prefix=".".join(parts))

    with patched as (m_handler,):
        assert main(*args) == m_handler.return_value.return_value
    assert m_handler.call_args == [args, {}]
    assert m_handler.return_value.call_args == [(), {}]


@pytest.fixture
def command_main(patches) -> Callable:
    return functools.partial(_command_main, patches)
