
from unittest.mock import AsyncMock

import pytest

from tools.base import aio


@pytest.mark.asyncio
async def test_async_subprocess_parallel(patches):
    patched = patches(
        "asyncio",
        "concurrent.futures",
        "async_subprocess.run",
        prefix="tools.base.aio")
    procs = [f"PROC{i}" for i in range(0, 3)]
    kwargs = {f"KEY{i}": f"VALUE{i}" for i in range(0, 3)}

    async def _result(result):
        return result

    with patched as (m_asyncio, m_future, m_run):
        _results = [f"RESULT{i}" for i in range(0, 5)]
        m_asyncio.as_completed.return_value = [
            _result(result) for result in _results]

        results = []
        async for result in aio.async_subprocess.parallel(procs, **kwargs):
            results.append(result)

    assert results == _results
    assert (
        list(m_future.ProcessPoolExecutor.call_args)
        == [(), {}])
    assert (
        list(m_asyncio.as_completed.call_args)
        == [(tuple(m_asyncio.ensure_future.return_value for i in range(0, len(procs))), ), {}])
    kwargs["executor"] = m_future.ProcessPoolExecutor.return_value.__enter__.return_value
    assert (
        list(list(c) for c in m_run.call_args_list)
        == [[(proc,), kwargs] for proc in procs])
    assert (
        list(list(c) for c in m_asyncio.ensure_future.call_args_list)
        == [[(m_run.return_value,), {}] for proc in procs])


@pytest.mark.asyncio
@pytest.mark.parametrize("loop", [True, False])
@pytest.mark.parametrize("executor", [None, "EXECUTOR"])
async def test_async_subprocess_run(patches, loop, executor):
    patched = patches(
        "asyncio",
        "partial",
        "subprocess",
        prefix="tools.base.aio")
    args = [f"ARG{i}" for i in range(0, 3)]
    kwargs = {f"KEY{i}": f"VALUE{i}" for i in range(0, 3)}

    if loop:
        kwargs["loop"] = AsyncMock()

    if executor:
        kwargs["executor"] = executor

    with patched as (m_asyncio, m_partial, m_subproc):
        m_asyncio.get_running_loop.return_value = AsyncMock()
        if loop:
            _loop = kwargs["loop"]
        else:
            _loop = m_asyncio.get_running_loop.return_value

        assert (
            await aio.async_subprocess.run(*args, **kwargs)
            == _loop.run_in_executor.return_value)

    if loop:
        assert not m_asyncio.get_running_loop.called

    kwargs.pop("executor", None)
    kwargs.pop("loop", None)

    assert (
        list(m_partial.call_args)
        == [(m_subproc.run, ) + tuple(args), kwargs])
    assert (
        list(_loop.run_in_executor.call_args)
        == [(executor, m_partial.return_value), {}])
