import importlib
import logging
import sys
from unittest.mock import MagicMock, patch, PropertyMock

import pytest

from tools.base import runner


# this is necessary to fix coverage as these libs are imported before pytest
# is invoked
importlib.reload(runner)


class DummyRunner(runner.Runner):

    def __init__(self):
        self.args = PropertyMock()


def test_runner_constructor():
    run = runner.Runner("path1", "path2", "path3")
    assert run._args == ("path1", "path2", "path3")


def test_runner_args():
    run = runner.Runner("path1", "path2", "path3")
    parser_mock = patch(
        "tools.base.runner.Runner.parser",
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


def test_runner_extra_args():
    run = runner.Runner("path1", "path2", "path3")
    parser_mock = patch(
        "tools.base.runner.Runner.parser",
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


def test_runner_log(patches):
    run = runner.Runner("path1", "path2", "path3")
    patched = patches(
        "logging.getLogger",
        "logging.StreamHandler",
        "LogFilter",
        ("Runner.log_level", dict(new_callable=PropertyMock)),
        ("Runner.name", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")

    with patched as (m_logger, m_stream, m_filter, m_level, m_name):
        _loggers = (MagicMock(), MagicMock())
        m_stream.side_effect = _loggers
        assert run.log == m_logger.return_value
    assert (
        list(m_logger.return_value.setLevel.call_args)
        == [(m_level.return_value,), {}])
    assert (
        list(list(c) for c in m_stream.call_args_list)
        == [[(sys.stdout,), {}],
            [(sys.stderr,), {}]])
    assert (
        list(_loggers[0].setLevel.call_args)
        == [(logging.DEBUG,), {}])
    assert (
        list(_loggers[0].addFilter.call_args)
        == [(m_filter.return_value,), {}])
    assert (
        list(_loggers[1].setLevel.call_args)
        == [(logging.WARN,), {}])
    assert (
        list(list(c) for c in m_logger.return_value.addHandler.call_args_list)
        == [[(_loggers[0],), {}], [(_loggers[1],), {}]])
    assert "log" in run.__dict__


def test_runner_log_level(patches):
    run = runner.Runner("path1", "path2", "path3")
    patched = patches(
        "dict",
        ("Runner.args", dict(new_callable=PropertyMock)),
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


def test_runner_name():
    run = DummyRunner()
    assert run.name == run.__class__.__name__
    assert "name" not in run.__dict__


def test_runner_parser(patches):
    run = runner.Runner("path1", "path2", "path3")
    patched = patches(
        "argparse.ArgumentParser",
        "Runner.add_arguments",
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


def test_checker_path():
    run = runner.Runner("path1", "path2", "path3")
    cwd_mock = patch("tools.base.runner.os.getcwd")

    with cwd_mock as m_cwd:
        assert run.path == m_cwd.return_value

    assert (
        list(m_cwd.call_args)
        == [(), {}])


def test_runner_add_arguments(patches):
    run = runner.Runner("path1", "path2", "path3")
    assert run.add_arguments("PARSER") is None


# LogFilter tests
@pytest.mark.parametrize("level", [logging.DEBUG, logging.INFO, logging.WARN, logging.ERROR, None, "giraffe"])
def test_runner_log_filter(level):
    logfilter = runner.LogFilter()

    class DummyRecord(object):
        levelno = level

    if level in [logging.DEBUG, logging.INFO]:
        assert logfilter.filter(DummyRecord())
    else:
        assert not logfilter.filter(DummyRecord())


# ForkingAdapter tests

def test_forkingadapter_constructor():
    _runner = DummyRunner()
    adapter = runner.ForkingAdapter(_runner)
    assert adapter.context == _runner


def test_forkingadapter_call():
    _runner = DummyRunner()
    adapter = runner.ForkingAdapter(_runner)
    fork_mock = patch("tools.base.runner.ForkingAdapter.fork")

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
def test_forkingadapter_fork(patches, args, cwd, capture_output):
    adapter = runner.ForkingAdapter(DummyRunner())
    patched = patches(
        "subprocess.run",
        ("Runner.path", dict(new_callable=PropertyMock)),
        prefix="tools.base.runner")

    with patched as (m_run, m_path):
        kwargs = {}
        if cwd != "NONE":
            kwargs["cwd"] = cwd
        if capture_output != "NONE":
            kwargs["capture_output"] = capture_output
        assert adapter.fork(*args, **kwargs) == m_run.return_value

    expected = {'capture_output': True, 'cwd': cwd}
    if capture_output is False:
        expected["capture_output"] = False
    if cwd == "NONE":
        expected["cwd"] = m_path.return_value
    assert (
        list(m_run.call_args)
        == [args, expected])
