
import asyncio
import gc
import inspect
import types
from typing import AsyncIterator, AsyncIterable
from unittest.mock import AsyncMock, MagicMock, PropertyMock

import pytest

from tools.base import aio


@pytest.mark.asyncio
async def test_async_subprocess_parallel(patches):
    patched = patches(
        "asyncio",
        "ProcessPoolExecutor",
        "async_subprocess.run",
        prefix="tools.base.aio")
    procs = [f"PROC{i}" for i in range(0, 3)]
    kwargs = {f"KEY{i}": f"VALUE{i}" for i in range(0, 3)}

    async def async_result(result):
        return result

    with patched as (m_asyncio, m_future, m_run):
        returned = [f"RESULT{i}" for i in range(0, 5)]
        m_asyncio.as_completed.return_value = [
            async_result(result) for result in returned]

        results = []
        async for result in aio.async_subprocess.parallel(procs, **kwargs):
            results.append(result)

    assert results == returned
    assert (
        list(m_future.call_args)
        == [(), {}])
    assert (
        list(m_asyncio.as_completed.call_args)
        == [(tuple(m_asyncio.ensure_future.return_value for i in range(0, len(procs))), ), {}])
    kwargs["executor"] = m_future.return_value.__enter__.return_value
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
            m_loop = kwargs["loop"]
        else:
            m_loop = m_asyncio.get_running_loop.return_value

        assert (
            await aio.async_subprocess.run(*args, **kwargs)
            == m_loop.run_in_executor.return_value)

    if loop:
        assert not m_asyncio.get_running_loop.called

    kwargs.pop("executor", None)
    kwargs.pop("loop", None)

    assert (
        list(m_partial.call_args)
        == [(m_subproc.run, ) + tuple(args), kwargs])
    assert (
        list(m_loop.run_in_executor.call_args)
        == [(executor, m_partial.return_value), {}])


@pytest.mark.parametrize("limit", ["XX", None, "", 0, -1, 73])
@pytest.mark.parametrize("yield_exceptions", [None, True, False])
def test_aio_concurrent_constructor(limit, yield_exceptions):
    kwargs = {}
    if limit == "XX":
        limit = None
    else:
        kwargs["limit"] = limit
    if yield_exceptions is not None:
        kwargs["yield_exceptions"] = yield_exceptions

    concurrent = aio.concurrent(["CORO"], **kwargs)
    assert concurrent._coros == ["CORO"]
    assert concurrent._limit == limit
    assert (
        concurrent.yield_exceptions
        == (False
            if yield_exceptions is None
            else yield_exceptions))
    assert concurrent._running == []

    assert concurrent.running_tasks is concurrent._running
    assert "running_tasks" in concurrent.__dict__


def test_aio_concurrent_dunder_aiter(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "asyncio",
        "concurrent.output",
        ("concurrent.submit", dict(new_callable=MagicMock)),
        prefix="tools.base.aio")

    with patched as (m_asyncio, m_output, m_submit):
        assert concurrent.__aiter__() == m_output.return_value

    assert concurrent.submit_task == m_asyncio.create_task.return_value
    assert (
        list(m_submit.call_args)
        == [(), {}])
    assert (
        list(m_asyncio.create_task.call_args)
        == [(m_submit.return_value, ), {}])


@pytest.mark.parametrize("running", [True, False])
@pytest.mark.parametrize("submitting", [True, False])
def test_aio_concurrent_active(patches, running, submitting):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "asyncio",
        ("concurrent.submitting", dict(new_callable=PropertyMock)),
        ("concurrent.running", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_asyncio, m_submit, m_run):
        m_submit.return_value = submitting
        m_run.return_value = running
        assert concurrent.active == (submitting or running)

    assert "active" not in concurrent.__dict__


def test_aio_concurrent_closing_lock(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "asyncio",
        prefix="tools.base.aio")

    with patched as (m_asyncio, ):
        assert concurrent.closing_lock == m_asyncio.Lock.return_value

    assert (
        list(m_asyncio.Lock.call_args)
        == [(), {}])
    assert "closing_lock" in concurrent.__dict__



@pytest.mark.parametrize("locked", [True, False])
def test_aio_concurrent_closed(patches, locked):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.closing_lock", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_closing_lock, ):
        m_closing_lock.return_value.locked.return_value = locked
        assert concurrent.closed == locked

    assert "closed" not in concurrent.__dict__


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [None, BaseException, GeneratorExit])
@pytest.mark.parametrize("close_raises", [None, BaseException])
async def test_aio_concurrent_coros(patches, raises, close_raises):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.iter_coros", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")
    results = []
    return_coros = [f"CORO{i}" for i in range(0, 3)]
    m_aclose = AsyncMock()
    if close_raises:
        m_aclose.side_effect = close_raises()

    class Coros:
        aclose = m_aclose

        def __call__(self):
            return self

        async def __aiter__(self):
            if raises:
                raise raises("AN ERROR OCCURRED")
            for coro in return_coros:
                yield coro

    with patched as (m_coros, ):
        coros = Coros()
        m_coros.return_value = coros
        if raises == BaseException:
            with pytest.raises(BaseException):
                async for coro in concurrent.coros:
                    pass
        else:
            async for coro in concurrent.coros:
                results.append(coro)

    if raises == GeneratorExit:
        assert (
            list(coros.aclose.call_args)
            == [(), {}])
        return

    assert not coros.aclose.called
    assert "coros" not in concurrent.__dict__

    if raises:
        return
    assert results == return_coros


def test_aio_concurrent_running_queue(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "asyncio",
        prefix="tools.base.aio")

    with patched as (m_asyncio, ):
        assert concurrent.running_queue == m_asyncio.Queue.return_value

    assert (
        list(m_asyncio.Queue.call_args)
        == [(), {}])
    assert "running_queue" in concurrent.__dict__


@pytest.mark.parametrize("cpus", [None, "", 0, 4, 73])
def test_aio_concurrent_default_limit(patches, cpus):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "min",
        "os",
        prefix="tools.base.aio")

    with patched as (m_min, m_os):
        m_os.cpu_count.return_value = cpus
        assert concurrent.default_limit == m_min.return_value

    assert (
        list(m_min.call_args)
        == [(32, (cpus or 0) + 4), {}])
    assert "default_limit" not in concurrent.__dict__


def test_aio_concurrent_consumes_async(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "isinstance",
        prefix="tools.base.aio")

    with patched as (m_inst, ):
        assert concurrent.consumes_async == m_inst.return_value

    assert (
        list(m_inst.call_args)
        == [(["CORO"], (types.AsyncGeneratorType, AsyncIterator, AsyncIterable)), {}])
    assert "consumes_async" in concurrent.__dict__


def test_aio_concurrent_consumes_generator(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "isinstance",
        prefix="tools.base.aio")

    with patched as (m_inst, ):
        assert concurrent.consumes_generator == m_inst.return_value

    assert (
        list(m_inst.call_args)
        == [(["CORO"], (types.AsyncGeneratorType, types.GeneratorType)), {}])
    assert "consumes_generator" in concurrent.__dict__


@pytest.mark.parametrize("limit", [None, "", 0, -1, 73])
def test_aio_concurrent_limit(patches, limit):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.default_limit", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")
    concurrent._limit = limit

    with patched as (m_limit, ):
        assert concurrent.limit == (limit or m_limit.return_value)

    if limit:
        assert not m_limit.called

    assert "limit" in concurrent.__dict__


@pytest.mark.parametrize("limit", [None, "", 0, -1, 73])
def test_aio_concurrent_nolimit(patches, limit):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.limit", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_limit, ):
        m_limit.return_value = limit
        assert concurrent.nolimit == (limit == -1)

    assert "nolimit" in concurrent.__dict__


def test_aio_concurrent_out(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "asyncio",
        prefix="tools.base.aio")

    with patched as (m_asyncio, ):
        assert concurrent.out == m_asyncio.Queue.return_value

    assert (
        list(m_asyncio.Queue.call_args)
        == [(), {}])
    assert "out" in concurrent.__dict__


@pytest.mark.parametrize("empty", [True, False])
def test_aio_concurrent_running(patches, empty):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.running_queue", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_running_queue, ):
        m_running_queue.return_value.empty.return_value = empty
        assert concurrent.running == (not empty)

    assert "running" not in concurrent.__dict__


def test_aio_concurrent_sem(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "asyncio",
        ("concurrent.limit", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_asyncio, m_limit):
        assert concurrent.sem == m_asyncio.Semaphore.return_value

    assert (
        list(m_asyncio.Semaphore.call_args)
        == [(m_limit.return_value, ), {}])
    assert "sem" in concurrent.__dict__


def test_aio_concurrent_submission_lock(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "asyncio",
        prefix="tools.base.aio")

    with patched as (m_asyncio, ):
        assert concurrent.submission_lock == m_asyncio.Lock.return_value

    assert (
        list(m_asyncio.Lock.call_args)
        == [(), {}])
    assert "submission_lock" in concurrent.__dict__


@pytest.mark.parametrize("locked", [True, False])
def test_aio_concurrent_submitting(patches, locked):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.submission_lock", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_submission_lock, ):
        m_submission_lock.return_value.locked.return_value = locked
        assert concurrent.submitting == locked

    assert "submitting" not in concurrent.__dict__


@pytest.mark.asyncio
async def test_aio_concurrent_cancel(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.cancel_tasks", dict(new_callable=AsyncMock)),
        ("concurrent.close", dict(new_callable=AsyncMock)),
        ("concurrent.close_coros", dict(new_callable=AsyncMock)),
        ("concurrent.sem", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    waiter = MagicMock()

    class SubmitTask:
        def __init__(self):
            self.cancel = MagicMock()

        def __await__(self):
            waiter()
            yield

    concurrent.submit_task = SubmitTask()

    with patched as (m_cancel, m_close, m_coros, m_sem):
        assert not await concurrent.cancel()

    assert (
        list(m_close.call_args)
        == [(), {}])
    assert (
        list(m_sem.return_value.release.call_args)
        == [(), {}])
    assert (
        list(m_cancel.call_args)
        == [(), {}])
    assert (
        list(m_coros.call_args)
        == [(), {}])
    assert (
        list(waiter.call_args)
        == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("bad", range(0, 8))
async def test_aio_concurrent_cancel_tasks(patches, bad):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.running_tasks", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    tasks = []
    waiter = MagicMock()

    class Task:
        def __init__(self, i):
            self.i = i
            self.cancel = MagicMock()

        def __await__(self):
            waiter()
            if self.i == bad:
                raise BaseException("AN ERROR OCCURRED")

    for i in range(0, 7):
        tasks.append(Task(i))

    with patched as (m_running, ):
        m_running.return_value = tasks
        assert not await concurrent.cancel_tasks()

    assert (
        list(list(c) for c in waiter.call_args_list)
        == [[(), {}]] * 7)
    for task in tasks:
        assert (
            list(task.cancel.call_args)
            == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("closed", [True, False])
async def test_aio_concurrent_close(patches, closed):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.closed", dict(new_callable=PropertyMock)),
        ("concurrent.closing_lock", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_closed, m_lock):
        m_closed.return_value = closed
        m_lock.return_value.acquire = AsyncMock()
        assert not await concurrent.close()

    if closed:
        assert not m_lock.called
    else:
        assert (
            list(m_lock.return_value.acquire.call_args)
            == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("consumes_generator", [True, False])
@pytest.mark.parametrize("bad", range(0, 8))
async def test_aio_concurrent_close_coros(patches, consumes_generator, bad):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "concurrent.close",
        ("concurrent.iter_coros", dict(new_callable=PropertyMock)),
        ("concurrent.consumes_generator", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    coros = []
    for i in range(0, 7):
        coro = MagicMock()
        if i == bad:
            coro.close.side_effect = BaseException("AN ERROR OCCURRED")
        coros.append(coro)

    async def iter_coros():
        for coro in coros:
            yield coro

    with patched as (m_close, m_iter, m_isgen):
        m_isgen.return_value = consumes_generator
        m_iter.return_value = iter_coros
        assert not await concurrent.close_coros()

    if consumes_generator:
        assert not m_iter.called
        return
    assert (
        list(m_iter.call_args)
        == [(), {}])
    for coro in coros:
        assert (
            list(coro.close.call_args)
            == [(), {}])


@pytest.mark.asyncio
async def test_aio_concurrent_create_task(patches):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "asyncio",
        "concurrent.remember_task",
        ("concurrent.task", dict(new_callable=MagicMock)),
        ("concurrent.running_queue", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_asyncio, m_rem, m_task, m_running_queue):
        assert not await concurrent.create_task("CORO")

    assert (
        list(m_running_queue.return_value.put_nowait.call_args)
        == [(None, ), {}])
    assert (
        list(m_task.call_args)
        == [("CORO", ), {}])
    assert (
        list(m_asyncio.create_task.call_args)
        == [(m_task.return_value, ), {}])
    assert (
        list(m_rem.call_args)
        == [(m_asyncio.create_task.return_value, ), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("closed", [True, False])
@pytest.mark.parametrize("active", [True, False])
async def test_aio_concurrent_exit_on_completion(patches, active, closed):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.active", dict(new_callable=PropertyMock)),
        ("concurrent.closed", dict(new_callable=PropertyMock)),
        ("concurrent.out", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    with patched as (m_active, m_closed, m_out):
        m_out.return_value.put = AsyncMock()
        m_active.return_value = active
        m_closed.return_value = closed
        assert not await concurrent.exit_on_completion()

    if closed or active:
        assert not m_out.called
        return
    assert (
        list(m_out.return_value.put.call_args)
        == [(aio._sentinel, ), {}])


@pytest.mark.parametrize("closed", [True, False])
def test_aio_concurrent_forget_task(patches, closed):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.closed", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")
    concurrent._running = MagicMock()

    with patched as (m_closed, ):
        m_closed.return_value = closed
        assert not concurrent.forget_task("TASK")

    if closed:
        assert not concurrent._running.remove.called
        return
    assert (
        list(concurrent._running.remove.call_args)
        == [("TASK", ), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [True, False])
@pytest.mark.parametrize("consumes_async", [True, False])
async def test_aio_concurrent_iter_coros(patches, raises, consumes_async):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.consumes_async", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    coros = [f"CORO{i}" for i in range(0, 7)]
    exception = BaseException("AN RAISES OCCURRED")

    def iter_coros():
        if raises:
            raise exception
        for coro in coros:
            yield coro

    async def async_iter_coros():
        if raises:
            raise exception
        for coro in coros:
            yield coro

    concurrent._coros = (
        async_iter_coros()
        if consumes_async
        else iter_coros())
    results = []

    with patched as (m_async, ):
        m_async.return_value = consumes_async

        async for result in concurrent.iter_coros():
            results.append(result)

    if raises:
        error = results[0]
        assert isinstance(error, aio.ConcurrentIteratorError)
        assert error.args[0] is exception
        assert results == [error]
        return
    assert results == coros


@pytest.mark.asyncio
@pytest.mark.parametrize("closed", [True, False])
@pytest.mark.parametrize("nolimit", [True, False])
@pytest.mark.parametrize("decrement", [None, True, False])
async def test_aio_concurrent_on_task_complete(patches, closed, nolimit, decrement):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.exit_on_completion", dict(new_callable=AsyncMock)),
        ("concurrent.closed", dict(new_callable=PropertyMock)),
        ("concurrent.out", dict(new_callable=PropertyMock)),
        ("concurrent.running_queue", dict(new_callable=PropertyMock)),
        ("concurrent.nolimit", dict(new_callable=PropertyMock)),
        ("concurrent.sem", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")
    kwargs = {}
    if decrement is not None:
        kwargs["decrement"] = decrement

    with patched as (m_complete, m_closed, m_out, m_running_queue, m_nolimit, m_sem):
        m_nolimit.return_value = nolimit
        m_closed.return_value = closed
        m_out.return_value.put = AsyncMock()
        assert not await concurrent.on_task_complete("RESULT", **kwargs)

    if closed:
        assert not m_complete.called
        assert not m_nolimit.called
        assert not m_sem.called
        assert not m_running_queue.called
        assert not m_out.return_value.put.called
        return

    assert (
        list(m_out.return_value.put.call_args)
        == [("RESULT", ), {}])
    if nolimit:
        assert not m_sem.return_value.release.called
    else:
        assert (
            list(m_sem.return_value.release.call_args)
            == [(), {}])
    if decrement or decrement is None:
        assert (
            list(m_running_queue.return_value.get_nowait.call_args)
            == [(), {}])
    else:
        assert not m_running_queue.return_value.get_nowait.called
    assert (
        list(m_complete.call_args)
        == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("result_count", range(0, 7))
@pytest.mark.parametrize("error", [True, False])
@pytest.mark.parametrize("should_error", [True, False])
async def test_aio_concurrent_output(patches, result_count, error, should_error):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "concurrent.should_error",
        ("concurrent.cancel", dict(new_callable=AsyncMock)),
        ("concurrent.close", dict(new_callable=AsyncMock)),
        ("concurrent.out", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    exception = Exception()

    class DummyQueue:
        _running_queue = 0

        async def get(self):
            if result_count == 0:
                return aio._sentinel
            if result_count > self._running_queue:
                self._running_queue += 1
                if error and result_count == self._running_queue:
                    return exception
                return f"RESULT {self._running_queue}"
            return aio._sentinel

        def should_error(self, result):
            return error and should_error and (result_count == self._running_queue)

    q = DummyQueue()
    results = []

    with patched as (m_error, m_cancel, m_close, m_out):
        m_out.return_value.get.side_effect = q.get
        m_error.side_effect = q.should_error
        if result_count and error and should_error:
            with pytest.raises(Exception):
                async for result in concurrent.output():
                    results.append(result)
        else:
            async for result in concurrent.output():
                results.append(result)

    if result_count and error and should_error:
        # last one errored
        assert results == [f"RESULT {i}" for i in range(1, result_count)]
        assert (
            list(list(c) for c in m_error.call_args_list)
            == [[(result,), {}] for result in results] + [[(exception,), {}]])
        assert (
            list(m_cancel.call_args)
            == [(), {}])
        assert not m_close.called
        return

    assert (
        list(list(c) for c in m_close.call_args_list)
        == [[(), {}]])
    assert not m_cancel.called

    if not result_count:
        assert results == []
        return

    if error:
        assert (
            results
            == [f"RESULT {i}" for i in range(1, result_count)] + [exception])
        return
    # all results returned correctly
    assert results == [f"RESULT {i}" for i in range(1, result_count + 1)]


@pytest.mark.asyncio
@pytest.mark.parametrize("closed_before", [True, False])
@pytest.mark.parametrize("closed_after", [True, False])
@pytest.mark.parametrize("nolimit", [True, False])
async def test_aio_concurrent_ready(patches, closed_before, closed_after, nolimit):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        ("concurrent.closed", dict(new_callable=PropertyMock)),
        ("concurrent.nolimit", dict(new_callable=PropertyMock)),
        ("concurrent.sem", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")

    class DummyCloser:
        order_mock = MagicMock()
        close_calls = 0

        async def _acquire(self):
            self.order_mock("ACQUIRE")

        def _nolimit(self):
            self.order_mock("NOLIMIT")
            return nolimit

        def _closed(self):
            self.order_mock("CLOSED")
            self.close_calls += 1
            if self.close_calls == 1:
                return closed_before
            if self.close_calls == 2:
                return closed_after

    closer = DummyCloser()

    with patched as (m_closed, m_nolimit, m_sem):
        m_nolimit.side_effect = closer._nolimit
        m_closed.side_effect = closer._closed
        m_sem.return_value.acquire = closer._acquire
        assert (
            await concurrent.ready()
            == ((not closed_before and not closed_after)
                if not nolimit else not closed_before))

    if closed_before:
        assert not m_nolimit.called
        assert not m_sem.called
        assert (
            list(list(c) for c in closer.order_mock.call_args_list)
            == [[('CLOSED',), {}]])
        return
    if nolimit:
        assert not m_sem.called
        assert (
            list(list(c) for c in closer.order_mock.call_args_list)
            == [[('CLOSED',), {}],
                [('NOLIMIT',), {}]])
        return
    assert (
        list(list(c) for c in closer.order_mock.call_args_list)
        == [[('CLOSED',), {}],
            [('NOLIMIT',), {}],
            [('ACQUIRE',), {}],
            [('CLOSED',), {}]])


def test_aio_concurrent_remember_task():
    concurrent = aio.concurrent(["CORO"])
    concurrent._running = MagicMock()
    task = MagicMock()
    assert not concurrent.remember_task(task)
    assert (
        list(concurrent._running.append.call_args)
        == [(task, ), {}])
    assert (
        list(task.add_done_callback.call_args)
        == [(concurrent.forget_task, ), {}])


@pytest.mark.parametrize("result", [None, "RESULT", aio.ConcurrentError, aio.ConcurrentExecutionError, aio.ConcurrentIteratorError])
@pytest.mark.parametrize("yield_exceptions", [True, False])
def test_aio_concurrent_should_error(result, yield_exceptions):
    concurrent = aio.concurrent(["CORO"])
    concurrent.yield_exceptions = yield_exceptions

    if isinstance(result, type) and issubclass(result, BaseException):
        result = result()

    assert (
        concurrent.should_error(result)
        == ((isinstance(result, aio.ConcurrentIteratorError)
             or isinstance(result, aio.ConcurrentError) and not yield_exceptions)))


@pytest.mark.asyncio
@pytest.mark.parametrize("coros", range(0, 7))
@pytest.mark.parametrize("unready", range(0, 8))
@pytest.mark.parametrize("valid_raises", [None, Exception, aio.ConcurrentError])
@pytest.mark.parametrize("iter_errors", [True, False])
async def test_aio_concurrent_submit(patches, coros, unready, valid_raises, iter_errors):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "isinstance",
        "concurrent.validate_coro",
        ("concurrent.exit_on_completion", dict(new_callable=AsyncMock)),
        ("concurrent.create_task", dict(new_callable=AsyncMock)),
        ("concurrent.on_task_complete", dict(new_callable=AsyncMock)),
        ("concurrent.ready", dict(new_callable=AsyncMock)),
        ("concurrent.coros", dict(new_callable=PropertyMock)),
        ("concurrent.out", dict(new_callable=PropertyMock)),
        ("concurrent.submission_lock", dict(new_callable=PropertyMock)),
        prefix="tools.base.aio")
    m_order = MagicMock()

    class DummyReady:
        counter = 0

        def ready(self):
            if self.counter >= unready:
                self.counter += 1
                return False
            self.counter += 1
            return True

    ready = DummyReady()

    async def acquire():
        m_order("ACQUIRE")

    def release():
        m_order("RELEASE")

    corolist = [MagicMock() for coro in range(1, coros)]

    async def iter_coros():
        for coro in corolist:
            m_order(coro)
            yield coro

    valid_errors = (
        (valid_raises == Exception)
        and coros > 1
        and not unready == 0
        and not iter_errors)

    with patched as (m_inst, m_valid, m_exit, m_create, m_complete, m_ready, m_coros, m_out, m_lock):
        m_out.return_value.put = AsyncMock()
        m_inst.return_value = iter_errors
        m_valid.side_effect = valid_raises
        m_ready.side_effect = ready.ready
        m_coros.return_value = iter_coros()
        m_lock.return_value.acquire.side_effect = acquire
        m_lock.return_value.release.side_effect = release

        if valid_errors:
            with pytest.raises(Exception):
                await concurrent.submit()
        else:
            assert not await concurrent.submit()

    if valid_errors:
        assert not m_lock.return_value.called
        assert not m_exit.called
    else:
        assert (
            list(m_lock.return_value.release.call_args)
            == [(), {}])
        assert (
            list(m_exit.call_args)
            == [(), {}])

    if coros < 2:
        assert not m_valid.called
        assert not m_inst.called
        assert not m_complete.called
        assert not m_create.called
        assert not m_ready.called
        assert not m_out.return_value.put.called
        return

    should_close_coro = (
        not iter_errors
        and not valid_errors
        and (len(corolist) > unready))

    if should_close_coro:
        assert corolist[unready].close.called
    else:
        assert not any(coro.close.called for coro in corolist)

    if iter_errors:
        assert (
            list(list(c) for c in m_out.return_value.put.call_args_list)
            == [[(corolist[0], ), {}]])
        assert (
            list(list(c) for c in m_inst.call_args_list)
            == [[(corolist[0], aio.ConcurrentIteratorError), {}]])
        assert not m_ready.called
        assert not m_valid.called
        assert not m_complete.called
        assert not m_create.called
        return

    if valid_errors:
        assert (
            list(list(c) for c in m_inst.call_args_list)
            == [[(corolist[0], aio.ConcurrentIteratorError), {}]])
        assert (
            list(list(c) for c in m_ready.call_args_list)
            == [[(), {}]])
        assert (
            list(list(c) for c in m_valid.call_args_list)
            == [[(corolist[0], ), {}]])
        assert not m_complete.called
        assert not m_create.called
        assert (
            list(list(c) for c in m_order.call_args_list)
            == ([[('ACQUIRE',), {}],
                 [(corolist[0],), {}]]))
        return

    assert not m_out.return_value.put.called
    assert (
        list(list(c) for c in m_ready.call_args_list)
        == [[(), {}]] * min(coros - 1, unready + 1 or 1))
    assert (
        list(list(c) for c in m_valid.call_args_list)
        == [[(corolist[i - 1], ), {}] for i in range(1, min(coros, unready + 1))])
    assert (
        list(list(c) for c in m_order.call_args_list)
        == ([[('ACQUIRE',), {}]]
            + [[(corolist[i - 1],), {}] for i in range(1, min(coros, unready + 2))]
            + [[('RELEASE',), {}]]))
    if valid_raises:
        assert len(m_complete.call_args_list) == max(min(coros - 1, unready), 0)
        for c in m_complete.call_args_list:
            error = list(c)[0][0]
            assert isinstance(error, aio.ConcurrentError)
            assert (
                list(c)
                == [(error,), {'decrement': False}])
        assert not m_create.called
        return
    assert not m_complete.called
    assert (
        list(list(c) for c in m_create.call_args_list)
        == [[(corolist[i - 1],), {}] for i in range(1, min(coros, unready + 1))])


class OtherException(BaseException):
    pass


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [None, Exception, OtherException])
async def test_aio_concurrent_task(patches, raises):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "concurrent.on_task_complete",
        prefix="tools.base.aio")

    if raises:
        exception = raises("AN ERROR OCCURRED")

    async def coro():
        if raises:
            raise exception
        return 23

    with patched as (m_complete, ):
        assert not await concurrent.task(coro())

    result = m_complete.call_args[0][0]

    if not raises:
        assert result == 23
    else:
        assert isinstance(result, aio.ConcurrentExecutionError)
        assert result.args[0] is exception
    assert (
        list(m_complete.call_args)
        == [(result, ), {}])


@pytest.mark.parametrize("awaitable", [True, False])
@pytest.mark.parametrize(
    "state",
    [inspect.CORO_CLOSED,
     inspect.CORO_CREATED,
     inspect.CORO_RUNNING,
     inspect.CORO_SUSPENDED])
def test_aio_concurrent_validate_coro(patches, awaitable, state):
    concurrent = aio.concurrent(["CORO"])
    patched = patches(
        "inspect.getcoroutinestate",
        prefix="tools.base.aio")

    # we cant patch inspect.isawaitable without fooing unittest
    def unawaitable():
        pass

    async def coro():
        pass

    awaits = (
        coro()
        if awaitable
        else unawaitable)

    with patched as (m_state, ):
        m_state.return_value = state

        if awaitable and state == inspect.CORO_CREATED:
            assert not concurrent.validate_coro(awaits)
        else:
            with pytest.raises(aio.ConcurrentError) as e:
                concurrent.validate_coro(awaits)

    if not awaitable:
        assert (
            e.value.args[0]
            == f'Provided input was not a coroutine: {awaits}')
        assert not m_state.called
        return

    awaits.close()
    assert (
        list(m_state.call_args)
        == [(awaits, ), {}])

    if state != inspect.CORO_CREATED:
        assert (
            e.value.args[0]
            == f'Provided coroutine has already been fired: {awaits}')


async def aiter(items):
    for item in items:
        yield item


@pytest.mark.asyncio
@pytest.mark.parametrize("limit", list(range(0, 4)) + [-1])
@pytest.mark.parametrize("yield_exceptions", [None, True, False])
@pytest.mark.parametrize("iter_type", [list, tuple, set, iter, aiter])
@pytest.mark.parametrize(
    "coros",
    [["HAPPY"],
     ["HAPPY"] * 2 + ["SAD"] + ["HAPPY"] * 3,
     ["HAPPY"] * 7,
     ["HAPPY"] * 2 + ["RAISE"] + ["HAPPY"] * 3,
     ["SAD"] * 2 + ["HAPPY"] * 3,
     ["HAPPY"] * 2 + ["CABBAGE"] + ["HAPPY"] * 3,
     ["HAPPY"] * 2 + ["FIRED"] + ["HAPPY"] * 3])
async def test_aio_concurrent_integration(limit, yield_exceptions, iter_type, coros):
    # This is an integration/black-box test that only measures inputs/outputs and the
    # effect of using the utility with them on them

    # `HAPPY` - a happy coroutine ready to be fired
    # `SAD` - a sad coroutine that will raise a `SadError` when fired
    # `FIRED` - a coroutine that has already been fired
    # `RAISE` - raise an error in the iterator
    # `CABBAGE` - leafy vegetable of the brassica family

    tasks_at_the_beginning = len(asyncio.all_tasks())

    kwargs = {}

    if yield_exceptions is not None:
        kwargs["yield_exceptions"] = yield_exceptions

    if limit:
        kwargs["limit"] = limit

    class SadError(Exception):
        pass

    class LoopError(Exception):
        pass

    async def happy():
        # this makes happy return after sad (ie errors) and tests the ordering of responses
        # and the handling of pending tasks when errors occur
        await asyncio.sleep(.01)
        return "HAPPY"

    fired = happy()
    await fired

    async def sad():
        raise SadError

    def coro_gen():
        for coro in coros:
            if coro == "RAISE":
                raise LoopError()
            if coro == "HAPPY":
                yield happy()
            elif coro == "SAD":
                yield sad()
            elif coro == "FIRED":
                yield fired
            else:
                yield coro

    all_good = all(coro == "HAPPY" for coro in coros)
    iter_raises = any(coro == "RAISE" for coro in coros)

    if iter_raises:
        # we can only test the generator types for errors
        # during iteration - ie if `list`, `tuple` etc contain
        # errors, they would raise now.
        if not iter_type in [iter, aiter]:
            return
        generated_coros = coro_gen()
    else:
        generated_coros = list(coro_gen())
        expected_err_index = next((i for i, x in enumerate(coros) if x != 'HAPPY'), None)

    results = []
    concurrent = aio.concurrent(iter_type(generated_coros), **kwargs)

    if (not all_good and not yield_exceptions) or iter_raises:
        if iter_raises:
            with pytest.raises(aio.ConcurrentIteratorError) as e:
                async for result in concurrent:
                    results.append(result)
            assert isinstance(e.value.args[0], LoopError)
            return
        else:
            coro_fail = (
                any(not inspect.isawaitable(coro) for coro in generated_coros)
                or any(coro == "FIRED" for coro in coros))
            if coro_fail:
                with pytest.raises(aio.ConcurrentError):
                    async for result in concurrent:
                        results.append(result)
            else:
                with pytest.raises(aio.ConcurrentExecutionError):
                    async for result in concurrent:
                        results.append(result)

        # for iterators there is no way of knowing that more awaitables were
        # on the way when failure happened, so these need to be closed here
        if iter_type in (iter, aiter):
            for coro in generated_coros[expected_err_index:]:
                if not isinstance(coro, str):
                    coro.close()

        if limit < 1 and iter_type != set:
            # as all jobs are submitted concurrently (the default is higher than
            # tne number of test jobs, and -1 forces no limit) and as sad is
            # faster than happy, we get no results
            assert results == []
        elif iter_type != set:
            # because the ordering on sets is indeterminate the results are unpredictable
            # therefore the easiest thing is to just exclude them from this test
            assert results == coros[:expected_err_index - (expected_err_index % limit)]

        # this can probs be removed, i think it was caused by unhandled GeneratorExit
        await asyncio.sleep(.001)
        gc.collect()
        assert len(asyncio.all_tasks()) == tasks_at_the_beginning
        return

    async for result in concurrent:
        results.append(result)

    assert len(asyncio.all_tasks()) == tasks_at_the_beginning

    def mangled_results():
        # replace the errors with the test strings
        for result in results:
            if isinstance(result, aio.ConcurrentExecutionError):
                yield "SAD"
            elif isinstance(result, aio.ConcurrentError):
                if "CABBAGE" in result.args[0]:
                    yield "CABBAGE"
                else:
                    yield "FIRED"
            else:
                yield result

    if expected_err_index:
        err_index = (
            expected_err_index
            if limit == 0
            else expected_err_index - (expected_err_index % limit))

    if expected_err_index and err_index >= limit and limit not in [0, -1]:
        # the error is at the beginning of whichever batch its in
        expected = ["HAPPY"] * 6
        expected[err_index] = coros[err_index]
    else:
        # the error is in the first batch so its at the beginning
        expected = [x for x in list(coros) if x != "HAPPY"] + [x for x in list(coros) if x == "HAPPY"]

    if iter_type == set:
        assert set(expected) == set(mangled_results())
    else:
        assert expected == list(mangled_results())
