
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


def test_all_pytests_pytest_targets():
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    bazel_mock = patch("tools.testing.all_pytests.PytestChecker.bazel", new_callable=PropertyMock)

    with bazel_mock as m_bazel:
        m_bazel.return_value.query.return_value = [
            "foo", ":pytest_foo",
            ":notpytest_foo", ":not_foo",
            "bar", "//asdf:pytest_barbaz"]
        assert (
            checker.pytest_targets
            == set([":pytest_foo", "//asdf:pytest_barbaz"]))
    assert (
        list(m_bazel.return_value.query.call_args)
        == [('tools/...',), {}])


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




def test_all_pytests_check_pytests(patches):
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    patched = patches(
        "PytestChecker.error",
        "PytestChecker.succeed",
        ("PytestChecker.pytest_targets", dict(new_callable=PropertyMock)),
        ("PytestChecker.bazel", dict(new_callable=PropertyMock)),
        prefix="tools.testing.all_pytests")

    check_runs = dict(
        check1=True,
        check2=True,
        check3=False,
        check4=False,
        check5=True,
        check6=False,
        check7=True)

    def _run_bazel(target):
        if not check_runs[target]:
            raise BazelRunError()

    with patched as (m_error, m_succeed, m_targets, m_bazel):
        m_targets.return_value = check_runs.keys()
        m_bazel.return_value.run.side_effect = _run_bazel
        checker.check_pytests()

    assert (
        list(list(c) for c in m_bazel.return_value.run.call_args_list)
        == [[('check1',), {}],
            [('check2',), {}],
            [('check3',), {}],
            [('check4',), {}],
            [('check5',), {}],
            [('check6',), {}],
            [('check7',), {}]])
    assert (
        list(list(c) for c in m_error.call_args_list)
        == [[('pytests', ['check3 failed']), {}],
            [('pytests', ['check4 failed']), {}],
            [('pytests', ['check6 failed']), {}]])
    assert (
        list(list(c) for c in m_succeed.call_args_list)
        == [[('pytests', ['check1']), {}],
            [('pytests', ['check2']), {}],
            [('pytests', ['check5']), {}],
            [('pytests', ['check7']), {}]])


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


@pytest.mark.parametrize("cov_html", ["", "SOMEPATH"])
def test_all_pytests_on_checks_complete(patches, cov_html):
    checker = all_pytests.PytestChecker("path1", "path2", "path3")
    patched = patches(
        ("PytestChecker.bazel", dict(new_callable=PropertyMock)),
        "checker.Checker.on_checks_complete",
        ("PytestChecker.cov_path", dict(new_callable=PropertyMock)),
        ("PytestChecker.cov_html", dict(new_callable=PropertyMock)),
        prefix="tools.testing.all_pytests")

    with patched as (m_bazel, m_complete, m_cov_path, m_cov_html):
        m_cov_html.return_value = cov_html
        assert checker.on_checks_complete() == m_complete.return_value
        assert (
            list(m_complete.call_args)
            == [(), {}])

    if cov_html:
        assert (
            list(m_bazel.return_value.run.call_args)
            == [('//tools/testing:python_coverage',
                 m_cov_path.return_value, cov_html), {}])
    else:
        assert not m_bazel.return_value.called


def test_coverage_main(command_main):
    command_main(
        all_pytests.main,
        "tools.testing.all_pytests.PytestChecker")
