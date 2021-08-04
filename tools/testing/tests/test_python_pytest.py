
import importlib
from unittest.mock import MagicMock, patch, PropertyMock

import pytest

from tools.testing import plugin, python_pytest


# this is necessary to fix coverage as these libs are imported before pytest
# is invoked
importlib.reload(python_pytest)
importlib.reload(plugin)


def test_pytest_cov_collect():
    runner = python_pytest.PytestRunner("path1", "path2", "path3")
    args_mock = patch("tools.testing.python_pytest.PytestRunner.args", new_callable=PropertyMock)

    with args_mock as m_args:
        assert runner.cov_collect == m_args.return_value.cov_collect


def test_pytest_add_arguments():
    runner = python_pytest.PytestRunner("path1", "path2", "path3")
    parser = MagicMock()
    runner.add_arguments(parser)
    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('--cov-collect',),
             {'default': None, 'help': 'Collect coverage data to path'}]])


def test_pytest_pytest_args(patches):
    runner = python_pytest.PytestRunner("path1", "path2", "path3")
    patched = patches(
        ("PytestRunner.extra_args", dict(new_callable=PropertyMock)),
        prefix="tools.testing.python_pytest")

    with patched as (m_args, ):
        assert (
            runner.pytest_args("COVERAGERC")
            == (m_args.return_value
                + ["--rcfile=COVERAGERC"]))


@pytest.mark.parametrize("cov_data", ["", "SOMEPATH"])
def test_pytest_run(patches, cov_data):
    runner = python_pytest.PytestRunner("path1", "path2", "path3")
    patched = patches(
        ("PytestRunner.cov_collect", dict(new_callable=PropertyMock)),
        ("PytestRunner.extra_args", dict(new_callable=PropertyMock)),
        "PytestRunner.pytest_args",
        "utils.coverage_with_data_file",
        "pytest.main",
        prefix="tools.testing.python_pytest")

    with patched as (m_cov_data, m_extra_args, m_py_args, m_cov_rc, m_main):
        m_cov_data.return_value = cov_data
        assert runner.run() == m_main.return_value

    if not cov_data:
        assert (
            list(m_main.call_args)
            == [(m_extra_args.return_value,), {}])
        assert not m_cov_rc.called
        assert not m_py_args.called
    else:
        assert (
            list(m_cov_rc.call_args)
            == [('SOMEPATH',), {}])
        assert (
            list(m_py_args.call_args)
            == [(m_cov_rc.return_value.__enter__.return_value,), {}])
        assert (
            list(m_main.call_args)
            == [(m_py_args.return_value,), {}])


def test_pytest_main(command_main):
    command_main(
        python_pytest.main,
        "tools.testing.python_pytest.PytestRunner")


def test_plugin_command_main(patches):
    patched = patches(
        "functools",
        "_command_main",
        prefix="tools.testing.plugin")

    with patched as (m_funct, m_command):
        assert plugin.command_main._pytestfixturefunction.scope == "function"
        assert plugin.command_main._pytestfixturefunction.autouse is False
        assert (
            plugin.command_main.__pytest_wrapped__.obj(patches)
            == m_funct.partial.return_value)

    assert (
        list(m_funct.partial.call_args)
        == [(m_command, patches), {}])


@pytest.mark.parametrize("args", [None, (), tuple(f"ARG{i}" for i in range(0, 3))])
@pytest.mark.parametrize("async_run", [None, True, False])
@pytest.mark.parametrize("raises", [None, "main", "handler", "run"])
def test_plugin__command_main(patches, args, async_run, raises):
    patched = patches(
        "_async_command_main",
        prefix="tools.testing.plugin")
    _args = ("arg0", "arg1", "arg2") if args is None else args
    _m_handler = MagicMock()
    _patches = MagicMock()
    _patches.return_value.__enter__.return_value = (_m_handler, )
    main = MagicMock()
    handler = MagicMock()
    kwargs = {}
    if args is not None:
        kwargs["args"] = args
    if async_run is not None:
        kwargs["async_run"] = async_run
    if raises != "main":
        main.return_value = _m_handler.return_value.run.return_value
    if raises != "handler":
        _m_handler(*_args)
    else:
        _m_handler("SOMETHING", "ELSE")
    if raises != "run":
        _m_handler.return_value.run()
    else:
        _m_handler.return_value.run("NOT", "RUN")

    with patched as (m_command, ):
        if not raises or async_run:
            result = plugin._command_main(_patches, main, handler, **kwargs)
        else:
            with pytest.raises(AssertionError) as e:
                plugin._command_main(_patches, main, handler, **kwargs)

    if async_run:
        assert result == m_command.return_value
        assert (
            list(m_command.call_args)
            == [(_patches,
                 main,
                 handler),
                {'args': _args}])
        assert not _patches.called
        assert not main.called
        return

    assert not m_command.called
    assert (
        list(_patches.call_args)
        == [(handler,), {}])
    assert (
        list(main.call_args)
        == [_args, {}])

    if not raises:
        assert not result


@pytest.mark.parametrize("raises", [None, "main", "aiorun", "handler", "run"])
def test_plugin__async_command_main(raises):
    _m_run = MagicMock()
    _m_handler = MagicMock()
    _patches = MagicMock()
    _patches.return_value.__enter__.return_value = (_m_run, _m_handler)
    main = MagicMock()
    handler = MagicMock()
    handler.split.return_value = [f"PART{i}" for i in range(0, 3)]
    args = ("arg0", "arg1", "arg2")

    if raises != "main":
        main.return_value = _m_run.return_value

    if raises != "aiorun":
        _m_run(_m_handler.return_value.run.return_value)
    else:
        _m_run("NOT", "AIORUN")
    if raises != "handler":
        _m_handler(*args)
    else:
        _m_handler("SOMETHING", "ELSE")
    if raises != "run":
        _m_handler.return_value.run()
    else:
        _m_handler.return_value.run("NOT", "RUN")

    if not raises:
        assert not plugin._async_command_main(_patches, main, handler, args)
    else:
        with pytest.raises(AssertionError):
            plugin._async_command_main(_patches, main, handler, args)

    assert (
        list(_patches.call_args)
        == [('asyncio.run', 'PART2'), {'prefix': 'PART0.PART1'}])
    assert (
        list(handler.split.call_args)
        == [('.',), {}])
    assert (
        list(main.call_args)
        == [args, {}])
