
from unittest.mock import patch, MagicMock, PropertyMock

import pytest

from tools.base.runner import BazelRunError
from tools.testing import all_pytests


def test_all_pytests_constructor():
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    assert checker.checks == ("pytests",)


@pytest.mark.parametrize("cov_collect", ["", "somepath"])
@pytest.mark.parametrize("cov_html", ["", "somepath"])
def test_all_pytests_cov_enabled(cov_collect, cov_html):
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    args_mock = patch(
        "tools.testing.all_pytests.PytestChecker.args",
        new_callable=PropertyMock)

    with args_mock as m_args:
        m_args.return_value.cov_collect = cov_collect
        m_args.return_value.cov_html = cov_html
        result = checker.cov_enabled

    if cov_collect or cov_html:
        assert result is True
    else:
        assert result is False
    assert "cov_enabled" not in checker.__dict__


@pytest.mark.parametrize("cov_html", ["", "somepath"])
def test_all_pytests_cov_html(cov_html):
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    args_mock = patch(
        "tools.testing.all_pytests.PytestChecker.args",
        new_callable=PropertyMock)

    with args_mock as m_args:
        m_args.return_value.cov_html = cov_html
        assert checker.cov_html == cov_html
    assert "cov_html" not in checker.__dict__


@pytest.mark.parametrize("cov_path", ["", "somepath"])
def test_all_pytests_cov_path(patches, cov_path):
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    patched = patches(
        "os.path.abspath",
        ("PytestChecker.args", dict(new_callable=PropertyMock)),
        prefix="tools.testing.all_pytests")

    with patched as (m_abspath, m_args):
        m_args.return_value.cov_collect = cov_path
        result = checker.cov_path

    if cov_path:
        assert result == cov_path
        assert not m_abspath.called
    else:
        assert result == m_abspath.return_value
        assert (
            list(m_abspath.call_args)
            == [('.coverage-envoy',), {}])
    assert "cov_path" not in checker.__dict__


@pytest.mark.parametrize("cov_enabled", [True, False])
def test_all_pytests_pytest_bazel_args(patches, cov_enabled):
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    patched = patches(
        ("PytestChecker.cov_path", dict(new_callable=PropertyMock)),
        ("PytestChecker.cov_enabled", dict(new_callable=PropertyMock)),
        prefix="tools.testing.all_pytests")

    with patched as (m_path, m_enabled):
        m_enabled.return_value = cov_enabled
        result = checker.pytest_bazel_args

    if cov_enabled:
        assert result == ['--cov-collect', m_path.return_value]
    else:
        assert result == []
    assert "pytest_bazel_args" not in checker.__dict__


def test_all_pytests_add_arguments():
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    parser = MagicMock()
    super_mock = patch("tools.testing.all_pytests.checker.BazelChecker.add_arguments")

    with super_mock as m_super:
        checker.add_arguments(parser)

    assert (
        list(m_super.call_args)
        == [(parser,), {}])
    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('--cov-collect',),
             {'default': None,
              'help': 'Specify a path to collect coverage with'}],
            [('--cov-html',),
             {'default': None,
              'help': 'Specify a path to collect html coverage with'}]])


@pytest.mark.parametrize("exists", [True, False])
@pytest.mark.parametrize("cov_path", ["", "SOMEPATH"])
def test_all_pytests_on_checks_begin(patches, exists, cov_path):
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    patched = patches(
        "os.path.exists",
        "os.unlink",
        ("PytestChecker.cov_path", dict(new_callable=PropertyMock)),
        prefix="tools.testing.all_pytests")

    with patched as (m_exists, m_unlink, m_cov_path):
        m_cov_path.return_value = cov_path
        m_exists.return_value = exists
        checker.on_checks_begin()

    if cov_path and exists:
        assert (
            list(m_unlink.call_args)
            == [('SOMEPATH',), {}])
    else:
        assert not m_unlink.called


def test_coverage_main(command_main):
    command_main(
        all_pytests.main,
        "tools.testing.all_pytests.PytestChecker")
