import importlib
import logging
import sys
from unittest.mock import AsyncMock, MagicMock, patch, PropertyMock

import pytest

from tools.base import runner


# this is necessary to fix coverage as these libs are imported before pytest
# is invoked
importlib.reload(runner)


class DummyRunner(runner.BaseRunner):

    def __init__(self):
        self.args = PropertyMock()


class DummyForkingRunner(runner.ForkingRunner):

    def __init__(self):
        self.args = PropertyMock()


class Error1(Exception):

    def __str__(self):
        return ""

    pass


class Error2(Exception):
    pass


def _failing_runner(errors):

    class DummyFailingRunner:
        # this dummy runner calls the _runner mock
        # when its run/run_async methods are called
        # and optionally raises some type of error
        # to ensure they are caught as expected

        log = PropertyMock()
        _runner = MagicMock()

        def __init__(self, raises=None):
            self.raises = raises

        @runner.catches(errors)
        def run(self, *args, **kwargs):
            result = self._runner(*args, **kwargs)
            if self.raises:
                raise self.raises("AN ERROR OCCURRED")
            return result

        @runner.catches(errors)
        async def run_async(self, *args, **kwargs):
            result = self._runner(*args, **kwargs)
            if self.raises:
                raise self.raises("AN ERROR OCCURRED")
            return result

    return DummyFailingRunner


@pytest.mark.asyncio
@pytest.mark.parametrize("async_fun", [True, False])
@pytest.mark.parametrize(
    "errors",
    [Error1, (Error1, Error2)])
@pytest.mark.parametrize(
    "raises",
    [None, Error1, Error2])
@pytest.mark.parametrize(
    "args",
    [(), ("ARG1", "ARG2")])
@pytest.mark.parametrize(
    "kwargs",
    [{}, dict(key1="VAL1", key2="VAL2")])
async def test_catches(errors, async_fun, raises, args, kwargs):
    run = _failing_runner(errors)(raises)
    should_fail = (
        raises
        and not (
            raises == errors
            or (isinstance(errors, tuple)
                and raises in errors)))

    assert run.run.__wrapped__.__catches__ == errors
    assert run.run_async.__wrapped__.__catches__ == errors

    if should_fail:
        result = 1
        with pytest.raises(raises):
            run.run(*args, **kwargs) if not async_fun else await run.run_async(*args, **kwargs)
    else:
        result = run.run(*args, **kwargs) if not async_fun else await run.run_async(*args, **kwargs)

    assert (
        list(run._runner.call_args)
        == [args, kwargs])

    if not should_fail and raises:
        assert result == 1
        error = run.log.error.call_args[0][0]
        _error = raises("AN ERROR OCCURRED")
        assert (
            error
            == (str(_error) or repr(_error)))
        assert (
            list(run.log.error.call_args)
            == [(error,), {}])
    else:
        assert not run.log.error.called

    if raises:
        assert result == 1
    else:
        assert result == run._runner.return_value


def _cleanup_runner(async_fun, raises):

    class DummyCleanupRunner:
        # this dummy runner calls the _runner mock
        # when its run/async_fun methods are called
        # and optionally raises some type of error
        # to ensure they are caught as expected

        log = PropertyMock()
        _runner = MagicMock()

        @runner.cleansup
        def run(self, *args, **kwargs):
            result = self._runner(*args, **kwargs)
            if raises:
                raise Exception("AN ERROR OCCURRED")
            return result

        @runner.cleansup
        async def run_async(self, *args, **kwargs):
            result = self._runner(*args, **kwargs)
            if raises:
                raise Exception("AN ERROR OCCURRED")
            return result

    return DummyCleanupRunner()


@pytest.mark.asyncio
@pytest.mark.parametrize("async_fun", [True, False])
@pytest.mark.parametrize("raises", [True, False])
async def test_cleansup(async_fun, raises):
    run = _cleanup_runner(async_fun, raises)
    args = [f"ARG{i}" for i in range(0, 3)]
    kwargs = {f"K{i}": f"V{i}" for i in range(0, 3)}

    assert run.run.__wrapped__.__cleansup__ is True
    assert run.run_async.__wrapped__.__cleansup__ is True

    if async_fun:
        run.cleanup = AsyncMock()
        if raises:
            with pytest.raises(Exception):
                await run.run_async(*args, **kwargs)
        else:
            assert (
                await run.run_async(*args, **kwargs)
                == run._runner.return_value)
    else:
        run.cleanup = MagicMock()
        if raises:
            with pytest.raises(Exception):
                run.run(*args, **kwargs)
        else:
            assert (
                run.run(*args, **kwargs)
                == run._runner.return_value)

    assert (
        list(run._runner.call_args)
        == [tuple(args), kwargs])
    assert (
        list(run.cleanup.call_args)
        == [(), {}])


def test_base_runner_constructor():
    run = runner.BaseRunner("path1", "path2", "path3")
    assert run._args == ("path1", "path2", "path3")
    assert run.log_field_styles == runner.LOG_FIELD_STYLES
    assert run.log_level_styles == runner.LOG_LEVEL_STYLES
    assert run.log_fmt == runner.LOG_FMT


def test_base_runner_args():
    run = runner.BaseRunner("path1", "path2", "path3")
    parser_mock = patch(
        "tools.base.runner.BaseRunner.parser",
        new_callable=PropertyMock)

    with parser_mock as m_parser:
        assert run.args == m_parser.return_value.parse_known_args.return_value.__getitem__.return_value

    assert (
        list(m_parser.return_value.parse_known_args.call_args)
        == [(('path1', 'path2', 'path3'),), {}])
    assert (
        list(m_parser.return_value.parse_known_args.return_value.__getitem__.call_args)
        == [(0,), {}])
    assert "args" in run.__dict__


def test_base_runner_extra_args():
    run = runner.BaseRunner("path1", "path2", "path3")
    parser_mock = patch(
        "tools.base.runner.BaseRunner.parser",
        new_callable=PropertyMock)

    with parser_mock as m_parser:
        assert run.extra_args == m_parser.return_value.parse_known_args.return_value.__getitem__.return_value

    assert (
        list(m_parser.return_value.parse_known_args.call_args)
        == [(('path1', 'path2', 'path3'),), {}])
    assert (
        list(m_parser.return_value.parse_known_args.return_value.__getitem__.call_args)
        == [(1,), {}])
    assert "extra_args" in run.__dict__


def test_base_runner_log(patches):
    run = runner.BaseRunner("path1", "path2", "path3")
    patched = patches(
        "logging.getLogger",
        "LogFilter",
        "coloredlogs",
        "verboselogs",
        ("BaseRunner.log_level", dict(new_callable=PropertyMock)),
        ("BaseRunner.log_level_styles", dict(new_callable=PropertyMock)),
        ("BaseRunner.log_field_styles", dict(new_callable=PropertyMock)),
        ("BaseRunner.log_fmt", dict(new_callable=PropertyMock)),
        ("BaseRunner.name", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")

    with patched as patchy:
        (m_logger, m_filter, m_color, m_verb,
         m_level, m_lstyle, m_fstyle, m_fmt, m_name) = patchy
        assert run.log == m_logger.return_value

    assert (
        list(m_verb.install.call_args)
        == [(), {}])
    assert (
        list(m_logger.return_value.setLevel.call_args)
        == [(m_level.return_value,), {}])
    assert (
        list(m_logger.return_value.setLevel.call_args)
        == [(m_level.return_value,), {}])
    assert (
        list(m_color.install.call_args)
        == [(),
            {'fmt': m_fmt.return_value,
             'isatty': True,
             'field_styles': m_fstyle.return_value,
             'level': 'DEBUG',
             'level_styles': m_lstyle.return_value,
             'logger': m_logger.return_value}])
    assert "log" in run.__dict__


def test_base_runner_log_level(patches):
    run = runner.BaseRunner("path1", "path2", "path3")
    patched = patches(
        "dict",
        ("BaseRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")
    with patched as (m_dict, m_args):
        assert run.log_level == m_dict.return_value.__getitem__.return_value

    assert (
        list(m_dict.call_args)
        == [(runner.LOG_LEVELS, ), {}])
    assert (
        list(m_dict.return_value.__getitem__.call_args)
        == [(m_args.return_value.log_level,), {}])
    assert "log_level" in run.__dict__


def test_base_runner_name():
    run = DummyRunner()
    assert run.name == run.__class__.__name__
    assert "name" not in run.__dict__


def test_base_runner_parser(patches):
    run = runner.BaseRunner("path1", "path2", "path3")
    patched = patches(
        "argparse.ArgumentParser",
        "BaseRunner.add_arguments",
        prefix="tools.base.runner")
    with patched as (m_parser, m_add_args):
        assert run.parser == m_parser.return_value

    assert (
        list(m_parser.call_args)
        == [(), {"allow_abbrev": False}])
    assert (
        list(m_add_args.call_args)
        == [(m_parser.return_value,), {}])
    assert "parser" in run.__dict__


def test_base_runner_path(patches):
    run = runner.BaseRunner("path1", "path2", "path3")
    patched = patches(
        "pathlib",
        prefix="tools.base.runner")

    with patched as (m_plib, ):
        assert run.path == m_plib.Path.return_value

    assert (
        list(m_plib.Path.call_args)
        == [(".", ), {}])


def test_base_runner_stdout(patches):
    run = runner.BaseRunner("path1", "path2", "path3")

    patched = patches(
        "logging",
        ("BaseRunner.log_level", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")

    with patched as (m_log, m_level):
        assert run.stdout == m_log.getLogger.return_value

    assert (
        list(m_log.getLogger.call_args)
        == [('stdout',), {}])
    assert (
        list(m_log.getLogger.return_value.setLevel.call_args)
        == [(m_level.return_value,), {}])
    assert (
        list(m_log.StreamHandler.call_args)
        == [(sys.stdout,), {}])
    assert (
        list(m_log.Formatter.call_args)
        == [('%(message)s',), {}])
    assert (
        list(m_log.StreamHandler.return_value.setFormatter.call_args)
        == [(m_log.Formatter.return_value,), {}])
    assert (
        list(m_log.getLogger.return_value.addHandler.call_args)
        == [(m_log.StreamHandler.return_value,), {}])


@pytest.mark.parametrize("missing", [True, False])
def test_base_runner_tempdir(patches, missing):
    run = runner.BaseRunner()
    patched = patches(
        "tempfile",
        ("BaseRunner.log", dict(new_callable=PropertyMock)),
        ("BaseRunner._missing_cleanup", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")

    with patched as (m_tmp, m_log, m_missing):
        m_missing.return_value = missing
        assert run.tempdir == m_tmp.TemporaryDirectory.return_value

    if missing:
        assert (
            list(m_log.return_value.warning.call_args)
            == [("Tempdir created but instance has a `run` method which is not decorated with `@runner.cleansup`", ), {}])
    else:
        assert not m_log.called

    assert (
        list(m_tmp.TemporaryDirectory.call_args)
        == [(), {}])
    assert "tempdir" in run.__dict__


def test_base_runner_add_arguments():
    run = runner.BaseRunner("path1", "path2", "path3")
    parser = MagicMock()

    assert run.add_arguments(parser) is None

    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('--log-level', '-l'),
             {'choices': ['debug', 'info', 'warn', 'error'],
              'default': 'info', 'help': 'Log level to display'}],
            ])


@pytest.mark.parametrize("has_fun", [True, False])
@pytest.mark.parametrize("is_wrapped", [True, False])
@pytest.mark.parametrize("cleansup", [True, False])
def test_base_runner__missing_cleanup(has_fun, is_wrapped, cleansup):

    def _runner_factory():
        if not has_fun:
            return runner.BaseRunner()

        class _Wrap:
            if cleansup:
                __cleansup__ = True

        class _Wrapper:
            if is_wrapped:
                __wrapped__ = _Wrap()

        class DummyRunner(runner.BaseRunner):
            run = _Wrapper()

        return DummyRunner()

    run = _runner_factory()

    assert (
        run._missing_cleanup
        == (has_fun
            and not (is_wrapped and cleansup)))
    assert "_missing_cleanup" not in run.__dict__


@pytest.mark.parametrize("cached", [True, False])
def test_base_runner__cleanup_tempdir(patches, cached):
    run = runner.BaseRunner()
    patched = patches(
        ("BaseRunner.tempdir", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")
    if cached:
        run.__dict__["tempdir"] = "TEMPDIR"

    with patched as (m_temp, ):
        assert not run._cleanup_tempdir()

    if cached:
        assert (
            list(m_temp.return_value.cleanup.call_args)
            == [(), {}])
    else:
        assert not m_temp.called
    assert "tempdir" not in run.__dict__


# LogFilter tests
@pytest.mark.parametrize("level", [logging.DEBUG, logging.INFO, logging.WARN, logging.ERROR, None, "giraffe"])
def test_base_runner_log_filter(level):
    logfilter = runner.LogFilter()

    class DummyRecord:
        levelno = level

    if level in [logging.DEBUG, logging.INFO]:
        assert logfilter.filter(DummyRecord())
    else:
        assert not logfilter.filter(DummyRecord())


def test_runner_constructor(patches):
    patched = patches(
        "BaseRunner.__init__",
        prefix="tools.base.runner")
    args = [f"ARG{i}" for i in range(0, 3)]
    kwargs = {f"K{i}": f"V{i}" for i in range(0, 3)}

    with patched as (m_super, ):
        m_super.return_value = None
        run = runner.Runner(*args, **kwargs)

    assert isinstance(run, runner.BaseRunner)
    assert (
        list(m_super.call_args)
        == [tuple(args), kwargs])


def test_runner_cleanup(patches):
    run = runner.Runner()
    patched = patches(
        "Runner._cleanup_tempdir",
        prefix="tools.base.runner")

    with patched as (m_temp, ):
        assert not run.cleanup()

    assert (
        list(m_temp.call_args)
        == [(), {}])


def test_async_runner_constructor(patches):
    patched = patches(
        "BaseRunner.__init__",
        prefix="tools.base.runner")
    args = [f"ARG{i}" for i in range(0, 3)]
    kwargs = {f"K{i}": f"V{i}" for i in range(0, 3)}

    with patched as (m_super, ):
        m_super.return_value = None
        run = runner.AsyncRunner(*args, **kwargs)

    assert isinstance(run, runner.BaseRunner)
    assert (
        list(m_super.call_args)
        == [tuple(args), kwargs])


@pytest.mark.asyncio
async def test_async_runner_cleanup(patches):
    run = runner.AsyncRunner()
    patched = patches(
        "AsyncRunner._cleanup_tempdir",
        prefix="tools.base.runner")

    with patched as (m_temp, ):
        assert not await run.cleanup()

    assert (
        list(m_temp.call_args)
        == [(), {}])


# BazelAdapter tests

def test_bazeladapter_constructor():
    run = DummyRunner()
    adapter = runner.BazelAdapter(run)
    assert adapter.context == run


@pytest.mark.parametrize("query_returns", [0, 1])
def test_bazeladapter_query(query_returns):
    run = DummyForkingRunner()
    adapter = runner.BazelAdapter(run)
    fork_mock = patch("tools.base.runner.ForkingAdapter.subproc_run")

    with fork_mock as m_fork:
        m_fork.return_value.returncode = query_returns
        if query_returns:
            with pytest.raises(runner.BazelRunError) as result:
                adapter.query("BAZEL QUERY")
        else:
            result = adapter.query("BAZEL QUERY")

    assert (
        list(m_fork.call_args)
        == [(['bazel', 'query', "'BAZEL QUERY'"],), {}])

    if query_returns:
        assert result.errisinstance(runner.BazelRunError)
        assert (
            result.value.args
            == (f"Bazel query failed: {m_fork.return_value}",))
        assert not m_fork.return_value.stdout.decode.called
    else:
        assert (
            result
            == m_fork.return_value.stdout.decode.return_value.split.return_value)
        assert (
            list(m_fork.return_value.stdout.decode.call_args)
            == [('utf-8',), {}])
        assert (
            list(m_fork.return_value.stdout.decode.return_value.split.call_args)
            == [('\n',), {}])


@pytest.mark.parametrize("cwd", [None, "", "SOMEPATH"])
@pytest.mark.parametrize("raises", [None, True, False])
@pytest.mark.parametrize("capture_output", [None, True, False])
@pytest.mark.parametrize("run_returns", [0, 1])
@pytest.mark.parametrize("args", [(), ("foo",), ("foo", "bar")])
def test_bazeladapter_run(patches, run_returns, cwd, raises, args, capture_output):
    run = DummyForkingRunner()
    adapter = runner.BazelAdapter(run)
    patched = patches(
        "ForkingAdapter.subproc_run",
        ("ForkingRunner.path", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")

    adapter_args = ("BAZEL RUN",) + args
    kwargs = {}
    if raises is not None:
        kwargs["raises"] = raises
    if cwd is not None:
        kwargs["cwd"] = cwd
    if capture_output is not None:
        kwargs["capture_output"] = capture_output

    with patched as (m_fork, m_path):
        m_fork.return_value.returncode = run_returns
        if run_returns and (raises is not False):
            with pytest.raises(runner.BazelRunError) as result:
                adapter.run(*adapter_args, **kwargs)
        else:
            result = adapter.run(*adapter_args, **kwargs)

    call_args = (("--",) + args) if args else args
    bazel_args = ("bazel", "run", "BAZEL RUN") + call_args
    bazel_kwargs = {}
    bazel_kwargs["capture_output"] = (
        True
        if capture_output is True
        else False)
    bazel_kwargs["cwd"] = (
        cwd
        if cwd
        else m_path.return_value)
    assert (
        list(m_fork.call_args)
        == [(bazel_args,), bazel_kwargs])
    if run_returns and (raises is not False):
        assert result.errisinstance(runner.BazelRunError)
        assert (
            result.value.args
            == (f"Bazel run failed: {m_fork.return_value}",))
    else:
        assert result == m_fork.return_value


# ForkingAdapter tests

def test_forkingadapter_constructor():
    run = DummyRunner()
    adapter = runner.ForkingAdapter(run)
    assert adapter.context == run


def test_forkingadapter_call():
    run = DummyRunner()
    adapter = runner.ForkingAdapter(run)
    fork_mock = patch("tools.base.runner.ForkingAdapter.subproc_run")

    with fork_mock as m_fork:
        assert (
            adapter(
                "arg1", "arg2", "arg3",
                kwa1="foo",
                kwa2="bar",
                kwa3="baz")
            == m_fork.return_value)
    assert (
        list(m_fork.call_args)
        == [('arg1', 'arg2', 'arg3'),
            {'kwa1': 'foo', 'kwa2': 'bar', 'kwa3': 'baz'}])


@pytest.mark.parametrize("args", [(), ("a", "b")])
@pytest.mark.parametrize("cwd", [None, "NONE", "PATH"])
@pytest.mark.parametrize("capture_output", ["NONE", True, False])
def test_forkingadapter_subproc_run(patches, args, cwd, capture_output):
    adapter = runner.ForkingAdapter(DummyRunner())
    patched = patches(
        "subprocess.run",
        ("BaseRunner.path", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")

    with patched as (m_run, m_path):
        kwargs = {}
        if cwd != "NONE":
            kwargs["cwd"] = cwd
        if capture_output != "NONE":
            kwargs["capture_output"] = capture_output
        assert adapter.subproc_run(*args, **kwargs) == m_run.return_value

    expected = {'capture_output': True, 'cwd': cwd}
    if capture_output is False:
        expected["capture_output"] = False
    if cwd == "NONE":
        expected["cwd"] = m_path.return_value
    assert (
        list(m_run.call_args)
        == [args, expected])


# ForkingRunner tests

def test_forkingrunner_fork():
    run = runner.ForkingRunner("path1", "path2", "path3")
    forking_mock = patch("tools.base.runner.ForkingAdapter")

    with forking_mock as m_fork:
        assert run.subproc_run == m_fork.return_value
    assert (
        list(m_fork.call_args)
        == [(run,), {}])
    assert "subproc_run" in run.__dict__


# BazelRunner tests

def test_bazelrunner_bazel():
    run = runner.BazelRunner("path1", "path2", "path3")
    bazel_mock = patch("tools.base.runner.BazelAdapter")

    with bazel_mock as m_bazel:
        assert run.bazel == m_bazel.return_value
    assert (
        list(m_bazel.call_args)
        == [(run,), {}])
    assert "bazel" in run.__dict__
