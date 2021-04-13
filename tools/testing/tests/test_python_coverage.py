
from unittest.mock import MagicMock, patch, PropertyMock

import pytest

from tools.testing import python_coverage


def test_coverage_cov_data():
    runner = python_coverage.CoverageRunner("path1", "path2", "path3")
    args_mock = patch("tools.testing.python_coverage.CoverageRunner.args", new_callable=PropertyMock)

    with args_mock as m_args:
        assert runner.cov_data == m_args.return_value.cov_data


def test_coverage_cov_html():
    runner = python_coverage.CoverageRunner("path1", "path2", "path3")
    args_mock = patch("tools.testing.python_coverage.CoverageRunner.args", new_callable=PropertyMock)

    with args_mock as m_args:
        assert runner.cov_html == m_args.return_value.cov_html


def test_coverage_coverage_args(patches):
    runner = python_coverage.CoverageRunner("path1", "path2", "path3")
    patched = patches(
        ("CoverageRunner.extra_args", dict(new_callable=PropertyMock)),
        ("CoverageRunner.cov_html", dict(new_callable=PropertyMock)),
        prefix="tools.testing.python_coverage")

    with patched as (m_args, m_cov_html):
        assert (
            runner.coverage_args("COVERAGERC")
            == (["html"]
                + m_args.return_value
                + ["--rcfile=COVERAGERC", "-d", m_cov_html.return_value]))


def test_coveragepytest_add_arguments():
    runner = python_coverage.CoverageRunner("path1", "path2", "path3")
    parser = MagicMock()
    runner.add_arguments(parser)
    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('cov_data',), {'help': 'Path to coverage data'}],
            [('cov_html',), {'help': 'Path to coverage html'}]])


@pytest.mark.parametrize("cov_data", ["", "SOMEPATH"])
def test_coverage_run(patches, cov_data):
    runner = python_coverage.CoverageRunner("path1", "path2", "path3")
    patched = patches(
        ("CoverageRunner.cov_data", dict(new_callable=PropertyMock)),
        ("CoverageRunner.extra_args", dict(new_callable=PropertyMock)),
        "CoverageRunner.coverage_args",
        "utils.coverage_with_data_file",
        "cmdline.main",
        prefix="tools.testing.python_coverage")

    with patched as (m_cov_data, m_extra_args, m_cov_args, m_cov_rc, m_main):
        m_cov_data.return_value = cov_data
        assert runner.run() == m_main.return_value

    if not cov_data:
        assert (
            list(m_main.call_args)
            == [(m_extra_args.return_value,), {}])
        assert not m_cov_rc.called
        assert not m_cov_args.called
    else:
        assert (
            list(m_cov_rc.call_args)
            == [('SOMEPATH',), {}])
        assert (
            list(m_cov_args.call_args)
            == [(m_cov_rc.return_value.__enter__.return_value,), {}])
        assert (
            list(m_main.call_args)
            == [(m_cov_args.return_value,), {}])


def test_coverage_main():
    class_mock = patch("tools.testing.python_coverage.CoverageRunner")

    with class_mock as m_class:
        assert (
            python_coverage.main("arg0", "arg1", "arg2")
            == m_class.return_value.run.return_value)

    assert (
        list(m_class.call_args)
        == [('arg0', 'arg1', 'arg2'), {}])
    assert (
        list(m_class.return_value.run.call_args)
        == [(), {}])
