
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


def test_pytest_main():
    class_mock = patch("tools.testing.python_pytest.PytestRunner")

    with class_mock as m_class:
        assert (
            python_pytest.main("arg0", "arg1", "arg2")
            == m_class.return_value.run.return_value)

    assert (
        list(m_class.call_args)
        == [('arg0', 'arg1', 'arg2'), {}])
    assert (
        list(m_class.return_value.run.call_args)
        == [(), {}])
