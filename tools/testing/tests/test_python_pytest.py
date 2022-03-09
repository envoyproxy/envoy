
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
        == [[('--verbosity', '-v'),
             {'choices': ['debug', 'info', 'warn', 'error'],
              'default': 'info',
              'help': 'Application log level'}],
            [('--log-level', '-l'),
             {'choices': ['debug', 'info', 'warn', 'error'],
              'default': 'warn',
              'help': 'Log level for non-application logs'}],
            [('--cov-collect',),
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
async def test_pytest_run(patches, cov_data):
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
        assert await runner.run() == m_main.return_value

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


@pytest.mark.parametrize("raises", [None, "main", "handler", "run"])
def test_plugin__command_main(raises):
    _m_handler = MagicMock()
    _patches = MagicMock()
    _patches.return_value.__enter__.return_value = (_m_handler, )
    main = MagicMock()
    handler = MagicMock()
    handler.split.return_value = [f"PART{i}" for i in range(0, 3)]
    args = ("arg0", "arg1", "arg2")

    if raises != "main":
        main.return_value = _m_handler.return_value.return_value

    if raises != "handler":
        _m_handler(*args)
    else:
        _m_handler("SOMETHING", "ELSE")
    if raises != "run":
        _m_handler.return_value()
    else:
        _m_handler.return_value("NOT", "RUN")

    if not raises:
        assert not plugin._command_main(_patches, main, handler, args)
    else:
        with pytest.raises(AssertionError):
            plugin._command_main(_patches, main, handler, args)

    assert (
        list(_patches.call_args)
        == [('PART2', ), {'prefix': 'PART0.PART1'}])
    assert (
        list(handler.split.call_args)
        == [('.',), {}])
    assert (
        list(main.call_args)
        == [args, {}])
