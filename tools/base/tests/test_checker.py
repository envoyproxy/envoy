import logging
from unittest.mock import MagicMock, patch, PropertyMock

import pytest

from tools.base.checker import AsyncChecker, BazelChecker, Checker, CheckerSummary, ForkingChecker
from tools.base.runner import BazelRunner, ForkingRunner


class DummyChecker(Checker):

    def __init__(self):
        self.args = PropertyMock()


class DummyForkingChecker(ForkingChecker):

    def __init__(self):
        self.args = PropertyMock()


class DummyBazelChecker(BazelChecker):

    def __init__(self):
        self.args = PropertyMock()


class DummyCheckerWithChecks(Checker):
    checks = ("check1", "check2")

    def __init__(self, *args):
        self.check1 = MagicMock()
        self.check2 = MagicMock()

    def check_check1(self):
        self.check1()

    def check_check2(self):
        self.check2()


def test_checker_constructor():
    super_mock = patch("tools.base.checker.runner.Runner.__init__")

    with super_mock as m_super:
        checker = Checker("path1", "path2", "path3")

    assert (
        list(m_super.call_args)
        == [('path1', 'path2', 'path3'), {}])
    assert checker.summary_class == CheckerSummary

    assert checker.active_check is None
    assert "active_check" not in checker.__dict__


def test_checker_diff():
    checker = Checker("path1", "path2", "path3")
    args_mock = patch(
        "tools.base.checker.Checker.args",
        new_callable=PropertyMock)

    with args_mock as m_args:
        assert checker.diff == m_args.return_value.diff
    assert "diff" not in checker.__dict__


@pytest.mark.parametrize(
    "errors",
    [{}, dict(exiting="EEK"), dict(notexiting="OK")])
def test_checker_exiting(errors):
    checker = Checker("path1", "path2", "path3")
    checker.errors = errors
    assert checker.exiting == bool("exiting" in errors)
    assert "exiting" not in checker.__dict__


def test_checker_error_count():
    checker = Checker("path1", "path2", "path3")
    checker.errors = dict(foo=["err"] * 3, bar=["err"] * 5, baz=["err"] * 7)
    assert checker.error_count == 15
    assert "error_count" not in checker.__dict__


def test_checker_failed():
    checker = Checker("path1", "path2", "path3")
    checker.errors = dict(foo=["err"] * 3, bar=["err"] * 5, baz=["err"] * 7)
    assert checker.failed == {'foo': 3, 'bar': 5, 'baz': 7}
    assert "failed" not in checker.__dict__


def test_checker_fix():
    checker = Checker("path1", "path2", "path3")
    args_mock = patch(
        "tools.base.checker.Checker.args",
        new_callable=PropertyMock)

    with args_mock as m_args:
        assert checker.fix == m_args.return_value.fix
    assert "fix" not in checker.__dict__


@pytest.mark.parametrize("failed", [True, False])
@pytest.mark.parametrize("warned", [True, False])
def test_checker_has_failed(patches, failed, warned):
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        ("Checker.failed", dict(new_callable=PropertyMock)),
        ("Checker.warned", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_failed, m_warned):
        m_failed.return_value = failed
        m_warned.return_value = warned
        result = checker.has_failed

    if failed or warned:
        assert result is True
    else:
        assert result is False
    assert "has_failed" not in checker.__dict__


@pytest.mark.parametrize("path", [None, "PATH"])
@pytest.mark.parametrize("paths", [[], ["PATH0"]])
@pytest.mark.parametrize("isdir", [True, False])
def test_checker_path(patches, path, paths, isdir):
    class DummyError(Exception):
        pass
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        ("Checker.args", dict(new_callable=PropertyMock)),
        ("Checker.parser", dict(new_callable=PropertyMock)),
        "os.path.isdir",
        prefix="tools.base.checker")

    with patched as (m_args, m_parser, m_isdir):
        m_parser.return_value.error = DummyError
        m_args.return_value.path = path
        m_args.return_value.paths = paths
        m_isdir.return_value = isdir
        if not path and not paths:
            with pytest.raises(DummyError) as e:
                checker.path
            assert (
                e.value.args
                == ('Missing path: `path` must be set either as an arg or with --path',))
        elif not isdir:
            with pytest.raises(DummyError) as e:
                checker.path
            assert (
                e.value.args
                == ('Incorrect path: `path` must be a directory, set either as first arg or with --path',))
        else:
            assert checker.path == path or paths[0]
            assert "path" in checker.__dict__
    if path or paths:
        assert (
            list(m_isdir.call_args)
            == [(path or paths[0],), {}])


@pytest.mark.parametrize("paths", [[], ["path1", "path2"]])
def test_checker_paths(patches, paths):
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        ("Checker.args", dict(new_callable=PropertyMock)),
        ("Checker.path", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_args, m_path):
        m_args.return_value.paths = paths
        result = checker.paths

    if paths:
        assert result == paths
    else:
        assert result == [m_path.return_value]
    assert "paths" not in checker.__dict__


@pytest.mark.parametrize("summary", [True, False])
@pytest.mark.parametrize("error_count", [0, 1])
@pytest.mark.parametrize("warning_count", [0, 1])
@pytest.mark.parametrize("exiting", [True, False])
def test_checker_show_summary(patches, summary, error_count, warning_count, exiting):
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        ("Checker.args", dict(new_callable=PropertyMock)),
        ("Checker.exiting", dict(new_callable=PropertyMock)),
        ("Checker.error_count", dict(new_callable=PropertyMock)),
        ("Checker.warning_count", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_args, m_exit, m_errors, m_warnings):
        m_args.return_value.summary = summary
        m_errors.return_value = error_count
        m_warnings.return_value = warning_count
        m_exit.return_value = exiting
        result = checker.show_summary

    if exiting:
        assert result is False
    elif summary or error_count or warning_count:
        assert result is True
    else:
        assert result is False
    assert "show_summary" not in checker.__dict__


def test_checker_status(patches):
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        ("Checker.success_count", dict(new_callable=PropertyMock)),
        ("Checker.error_count", dict(new_callable=PropertyMock)),
        ("Checker.warning_count", dict(new_callable=PropertyMock)),
        ("Checker.failed", dict(new_callable=PropertyMock)),
        ("Checker.warned", dict(new_callable=PropertyMock)),
        ("Checker.succeeded", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as args:
        (m_success_count, m_error_count, m_warning_count,
         m_failed, m_warned, m_succeeded) = args
        assert (
            checker.status
            == dict(
                success=m_success_count.return_value,
                errors=m_error_count.return_value,
                warnings=m_warning_count.return_value,
                failed=m_failed.return_value,
                warned=m_warned.return_value,
                succeeded=m_succeeded.return_value))
    assert "status" not in checker.__dict__


def test_checker_succeeded():
    checker = Checker("path1", "path2", "path3")
    checker.success = dict(
        foo=["check"] * 3,
        bar=["check"] * 5,
        baz=["check"] * 7)
    assert (
        checker.succeeded
        == dict(foo=3, bar=5, baz=7))
    assert "succeeded" not in checker.__dict__


def test_checker_success_count():
    checker = Checker("path1", "path2", "path3")
    checker.success = dict(foo=["err"] * 3, bar=["err"] * 5, baz=["err"] * 7)
    assert checker.success_count == 15
    assert "success_count" not in checker.__dict__


def test_checker_summary():
    checker = Checker("path1", "path2", "path3")
    summary_mock = patch(
        "tools.base.checker.Checker.summary_class",
        new_callable=PropertyMock)

    with summary_mock as m_summary:
        assert checker.summary == m_summary.return_value.return_value

    assert (
        list(m_summary.return_value.call_args)
        == [(checker,), {}])
    assert "summary" in checker.__dict__


def test_checker_warned():
    checker = Checker("path1", "path2", "path3")
    checker.warnings = dict(
        foo=["check"] * 3,
        bar=["check"] * 5,
        baz=["check"] * 7)
    assert (
        checker.warned
        == dict(foo=3, bar=5, baz=7))
    assert "warned" not in checker.__dict__


def test_checker_warning_count():
    checker = Checker("path1", "path2", "path3")
    checker.warnings = dict(foo=["warn"] * 3, bar=["warn"] * 5, baz=["warn"] * 7)
    assert checker.warning_count == 15
    assert "warning_count" not in checker.__dict__


def test_checker_add_arguments(patches):
    checker = DummyCheckerWithChecks("path1", "path2", "path3")
    parser = MagicMock()
    patched = patches(
        "runner.Runner.add_arguments",
        prefix="tools.base.checker")

    with patched as (m_super, ):
        assert checker.add_arguments(parser) is None

    assert (
        list(m_super.call_args)
        == [(parser,), {}])

    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('--fix',),
             {'action': 'store_true',
              'default': False,
              'help': 'Attempt to fix in place'}],
            [('--diff',),
             {'action': 'store_true',
              'default': False,
              'help': 'Display a diff in the console where available'}],
            [('--warning', '-w'),
             {'choices': ['warn', 'error'],
              'default': 'warn',
              'help': 'Handle warnings as warnings or errors'}],
            [('--summary',),
             {'action': 'store_true',
              'default': False,
              'help': 'Show a summary of check runs'}],
            [('--summary-errors',),
             {'type': int,
              'default': 5,
              'help': 'Number of errors to show in the summary, -1 shows all'}],
            [('--summary-warnings',),
             {'type': int,
              'default': 5,
              'help': 'Number of warnings to show in the summary, -1 shows all'}],
            [('--check', '-c'),
             {'choices': ("check1", "check2"),
              'nargs': '*',
              'help': 'Specify which checks to run, can be specified for multiple checks'}],
            [('--config-check1',),
             {'default': '',
              'help': 'Custom configuration for the check1 check'}],
            [('--config-check2',),
             {'default': '',
              'help': 'Custom configuration for the check2 check'}],
            [('--path', '-p'),
             {'default': None,
              'help': 'Path to the test root (usually Envoy source dir). If not specified the first path of paths is used'}],
            [('paths',),
             {'nargs': '*',
              'help': 'Paths to check. At least one path must be specified, or the `path` argument should be provided'}]])


TEST_ERRORS = (
    {},
    dict(myerror=[]),
    dict(myerror=["a", "b", "c"]),
    dict(othererror=["other1", "other2", "other3"]),
    dict(othererror=["other1", "other2", "other3"], myerror=["a", "b", "c"]))


@pytest.mark.parametrize("log", [True, False])
@pytest.mark.parametrize("log_type", [None, "fatal"])
@pytest.mark.parametrize("errors", TEST_ERRORS)
@pytest.mark.parametrize("newerrors", [[], ["err1", "err2", "err3"]])
def test_checker_error(log, log_type, errors, newerrors):
    checker = Checker("path1", "path2", "path3")
    log_mock = patch(
        "tools.base.checker.Checker.log",
        new_callable=PropertyMock)
    checker.errors = errors.copy()
    result = 1 if newerrors else 0

    with log_mock as m_log:
        if log_type:
            assert checker.error("mycheck", newerrors, log, log_type=log_type) == result
        else:
            assert checker.error("mycheck", newerrors, log) == result

    if not newerrors:
        assert not m_log.called
        assert "mycheck" not in checker.errors
        return

    assert checker.errors["mycheck"] == errors.get("mycheck", []) + newerrors
    for k, v in errors.items():
        if k != "mycheck":
            assert checker.errors[k] == v
    if log:
        assert (
            list(list(c) for c in getattr(m_log.return_value, log_type or "error").call_args_list)
            == [[(f'[mycheck] err{i}',), {}] for i in range(1, 4)])
    else:
        assert not getattr(m_log.return_value, log_type or "error").called


def test_checker_exit(patches):
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        "Checker.error",
        ("Checker.log", dict(new_callable=PropertyMock)),
        ("Checker.stdout", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_error, m_log, m_stdout):
        assert checker.exit() == m_error.return_value

    assert (
        list(m_log.return_value.handlers.__getitem__.call_args)
        == [(0,), {}])
    assert (
        list(m_log.return_value.handlers.__getitem__.return_value.setLevel.call_args)
        == [(logging.FATAL,), {}])
    assert (
        list(m_stdout.return_value.handlers.__getitem__.call_args)
        == [(0,), {}])
    assert (
        list(m_stdout.return_value.handlers.__getitem__.return_value.setLevel.call_args)
        == [(logging.FATAL,), {}])
    assert (
        list(m_error.call_args)
        == [('exiting', ['Keyboard exit']), {'log_type': 'fatal'}])


TEST_CHECKS = (
    None,
    (),
    ("check1", ),
    ("check1", "check2", "check3"),
    ("check3", "check4", "check5"),
    ("check4", "check5"))


@pytest.mark.parametrize("checks", TEST_CHECKS)
def test_checker_get_checks(checks):
    checker = Checker("path1", "path2", "path3")
    checker.checks = ("check1", "check2", "check3")
    args_mock = patch(
        "tools.base.checker.Checker.args",
        new_callable=PropertyMock)

    with args_mock as m_args:
        m_args.return_value.check = checks
        if checks:
            assert (
                checker.get_checks()
                == [check for check in checker.checks if check in checks or []])
        else:
            assert checker.get_checks() == checker.checks


def test_checker_on_check_begin(patches):
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        ("Checker.log", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_log, ):
        assert not checker.on_check_begin("checkname")

    assert checker.active_check == "checkname"
    assert (
        list(m_log.return_value.notice.call_args)
        == [('[checkname] Running check',), {}])


@pytest.mark.parametrize("errors", [[], ["CHECK1", "CHECK2", "CHECK3"], ["CHECK2", "CHECK3"]])
@pytest.mark.parametrize("warnings", [[], ["CHECK1", "CHECK2", "CHECK3"], ["CHECK2", "CHECK3"]])
@pytest.mark.parametrize("exiting", [True, False])
def test_checker_on_check_run(patches, errors, warnings, exiting):
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        ("Checker.exiting", dict(new_callable=PropertyMock)),
        ("Checker.log", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    check = "CHECK1"
    checker.errors = errors
    checker.warnings = warnings
    checker._active_check = check

    with patched as (m_exit, m_log):
        m_exit.return_value = exiting
        assert not checker.on_check_run(check)

    assert checker.active_check is None

    if exiting:
        assert not m_log.called
        return

    if check in errors:
        assert (
            list(m_log.return_value.error.call_args)
            == [('[CHECK1] Check failed',), {}])
        assert not m_log.return_value.warning.called
        assert not m_log.return_value.success.called
        return

    if check in warnings:
        assert (
            list(m_log.return_value.warning.call_args)
            == [('[CHECK1] Check has warnings',), {}])
        assert not m_log.return_value.error.called
        assert not m_log.return_value.info.called
        return

    assert (
        list(m_log.return_value.success.call_args)
        == [(f'[{check}] Check completed successfully',), {}])
    assert not m_log.return_value.warning.called
    assert not m_log.return_value.error.called


def test_checker_on_checks_begin():
    checker = Checker("path1", "path2", "path3")
    assert checker.on_checks_begin() is None


@pytest.mark.parametrize("failed", [True, False])
@pytest.mark.parametrize("show_summary", [True, False])
def test_checker_on_checks_complete(patches, failed, show_summary):
    checker = Checker("path1", "path2", "path3")
    patched = patches(
        ("Checker.has_failed", dict(new_callable=PropertyMock)),
        ("Checker.show_summary", dict(new_callable=PropertyMock)),
        ("Checker.summary", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_failed, m_show_summary, m_summary):
        m_failed.return_value = failed
        m_show_summary.return_value = show_summary
        assert checker.on_checks_complete() is (1 if failed else 0)

    if show_summary:
        assert (
            list(m_summary.return_value.print_summary.call_args)
            == [(), {}])
    else:
        assert not m_summary.return_value.print_summary.called


@pytest.mark.parametrize("raises", [None, KeyboardInterrupt, Exception])
def test_checker_run(patches, raises):
    checker = DummyCheckerWithChecks("path1", "path2", "path3")
    patched = patches(
        "Checker.exit",
        "Checker.get_checks",
        "Checker.on_check_begin",
        "Checker.on_check_run",
        "Checker.on_checks_begin",
        "Checker.on_checks_complete",
        ("Checker.log", dict(new_callable=PropertyMock)),
        ("Checker.name", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_exit, m_get, m_check, m_run, m_begin, m_complete, m_log, m_name):
        m_get.return_value = ("check1", "check2")

        if raises:
            m_begin.side_effect = raises()

            if raises == KeyboardInterrupt:
                result = checker.run()

            else:
                with pytest.raises(raises):
                    checker.run()
        else:
            assert checker.run() == m_complete.return_value

    assert (
        list(m_begin.call_args)
        == [(), {}])
    assert (
        list(m_complete.call_args)
        == [(), {}])

    if raises == KeyboardInterrupt:
        assert (
            list(m_exit.call_args)
            == [(), {}])
        return

    assert not m_exit.called

    if raises:
        return

    assert (
        list(m_get.call_args)
        == [(), {}])
    assert (
        list(list(c) for c in m_check.call_args_list)
        == [[(f'check{i}',), {}] for i in range(1, 3)])
    assert (
        list(list(c) for c in m_run.call_args_list)
        == [[(f'check{i}',), {}] for i in range(1, 3)])
    assert (
        list(checker.check1.call_args)
        == [(), {}])
    assert (
        list(checker.check2.call_args)
        == [(), {}])


TEST_WARNS = (
    {},
    dict(mywarn=[]),
    dict(mywarn=["a", "b", "c"]),
    dict(otherwarn=["other1", "other2", "other3"]),
    dict(otherwarn=["other1", "other2", "other3"], mywarn=["a", "b", "c"]))


@pytest.mark.parametrize("log", [True, False])
@pytest.mark.parametrize("warns", TEST_WARNS)
def test_checker_warn(patches, log, warns):
    checker = Checker("path1", "path2", "path3")
    log_mock = patch(
        "tools.base.checker.Checker.log",
        new_callable=PropertyMock)
    checker.warnings = warns.copy()

    with log_mock as m_log:
        checker.warn("mycheck", ["warn1", "warn2", "warn3"], log)

    assert checker.warnings["mycheck"] == warns.get("mycheck", []) + ["warn1", "warn2", "warn3"]
    for k, v in warns.items():
        if k != "mycheck":
            assert checker.warnings[k] == v
    if log:
        assert (
            list(list(c) for c in m_log.return_value.warning.call_args_list)
            == [[(f'[mycheck] warn{i}',), {}] for i in range(1, 4)])
    else:
        assert not m_log.return_value.warn.called


TEST_SUCCESS = (
    {},
    dict(mysuccess=[]),
    dict(mysuccess=["a", "b", "c"]),
    dict(othersuccess=["other1", "other2", "other3"]),
    dict(othersuccess=["other1", "other2", "other3"], mysuccess=["a", "b", "c"]))


@pytest.mark.parametrize("log", [True, False])
@pytest.mark.parametrize("success", TEST_SUCCESS)
def test_checker_succeed(patches, log, success):
    checker = Checker("path1", "path2", "path3")
    log_mock = patch(
        "tools.base.checker.Checker.log",
        new_callable=PropertyMock)
    checker.success = success.copy()

    with log_mock as m_log:
        checker.succeed("mycheck", ["success1", "success2", "success3"], log)

    assert checker.success["mycheck"] == success.get("mycheck", []) + ["success1", "success2", "success3"]
    for k, v in success.items():
        if k != "mycheck":
            assert checker.success[k] == v
    if log:
        assert (
            list(list(c) for c in m_log.return_value.success.call_args_list)
            == [[(f'[mycheck] success{i}',), {}] for i in range(1, 4)])
    else:
        assert not m_log.return_value.success.called


# CheckerSummary tests

def test_checker_summary_constructor():
    checker = DummyChecker()
    summary = CheckerSummary(checker)
    assert summary.checker == checker


@pytest.mark.parametrize("max_errors", [-1, 0, 1, 23])
def test_checker_summary_max_errors(max_errors):
    checker = DummyChecker()
    summary = CheckerSummary(checker)
    checker.args.summary_errors = max_errors
    assert summary.max_errors == max_errors


@pytest.mark.parametrize("max_warnings", [-1, 0, 1, 23])
def test_checker_summary_max_warnings(max_warnings):
    checker = DummyChecker()
    summary = CheckerSummary(checker)
    checker.args.summary_warnings = max_warnings
    assert summary.max_warnings == max_warnings


def test_checker_summary_print_summary(patches):
    checker = DummyChecker()
    summary = CheckerSummary(checker)
    patched = patches(
        "CheckerSummary.print_failed",
        "CheckerSummary.print_status",
        prefix="tools.base.checker")

    with patched as (m_failed, m_status):
        summary.print_summary()
    assert (
        list(list(c) for c in m_failed.call_args_list)
        == [[('warnings',), {}], [('errors',), {}]])
    assert m_status.called


TEST_SECTIONS = (
    ("MSG1", ["a", "b", "c"]),
    ("MSG2", []),
    ("MSG3", None))


@pytest.mark.parametrize("section", TEST_SECTIONS)
def test_checker_summary_section(section):
    checker = DummyChecker()
    summary = CheckerSummary(checker)
    message, lines = section
    expected = [
        "Summary",
        "-" * 80,
        f"{message}"]
    if lines:
        expected += lines
    assert summary._section(message, lines) == expected


@pytest.mark.parametrize("errors", (True, False))
@pytest.mark.parametrize("warnings", (True, False))
def test_checker_summary_print_status(patches, errors, warnings):
    checker = DummyChecker()
    summary = CheckerSummary(checker)
    summary.checker = MagicMock()
    summary.checker.errors = errors
    summary.checker.warnings = warnings

    assert not summary.print_status()

    if errors:
        assert (
            list(summary.checker.log.error.call_args)
            == [(f"{summary.checker.status}",), {}])
        assert not summary.checker.log.warning.called
        assert not summary.checker.log.info.called
        return

    if warnings:
        assert (
            list(summary.checker.log.warning.call_args)
            == [(f"{summary.checker.status}",), {}])
        assert not summary.checker.log.error.called
        assert not summary.checker.log.info.called
        return

    assert (
        list(summary.checker.log.info.call_args)
        == [(f"{summary.checker.status}",), {}])
    assert not summary.checker.log.error.called
    assert not summary.checker.log.warning.called


@pytest.mark.parametrize("problem_type", ("errors", "warnings"))
@pytest.mark.parametrize("max_display", (-1, 0, 1, 23))
@pytest.mark.parametrize("problems", ({}, dict(foo=["problem1"]), dict(foo=["problem1", "problem2"], bar=["problem3", "problem4"])))
def test_checker_summary_print_failed(patches, problem_type, max_display, problems):
    checker = DummyChecker()
    summary = CheckerSummary(checker)
    patched = patches(
        "CheckerSummary._section",
        (f"CheckerSummary.max_{problem_type}", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_section, m_max):
        summary.checker = MagicMock()
        setattr(summary.checker, f"{problem_type}", problems)
        m_max.return_value = max_display
        m_section.return_value = ["A", "B", "C"]
        summary.print_failed(problem_type)

    if not problems:
        assert not summary.checker.log.error.called
        assert not m_section.called
        return

    output = (
        summary.checker.log.warning if problem_type == "warnings" else summary.checker.log.error)

    assert (
        list(output.call_args)
        == [("".join(['A\nB\nC\n'] * len(problems)),), {}])

    if max_display == 0:
        expected = [
            [(f"{summary.checker.name} {prob}", []), {}]
            for prob in problems]
    else:
        def _problems(prob):
            return (
                problems[prob][:max_display]
                if max_display > 0
                else problems[prob])
        def _extra(prob):
            return (
                f": (showing first {max_display} of {len(problems)})"
                if len(problems[prob]) > max_display and max_display >= 0
                else (":"
                      if max_display != 0
                      else ""))
        expected = [
            [(f"{summary.checker.name} {prob}{_extra(prob)}", _problems(prob)), {}]
            for prob in problems]
    assert (
        list(list(c) for c in m_section.call_args_list)
        == expected)


# ForkingChecker test

def test_forkingchecker_constructor():
    checker = DummyForkingChecker()
    assert isinstance(checker, ForkingRunner)
    assert isinstance(checker, Checker)


# BazelChecker test

def test_bazelchecker_constructor():
    checker = DummyBazelChecker()
    assert isinstance(checker, BazelRunner)
    assert isinstance(checker, Checker)


# AsyncChecker tests

def test_asynchecker_constructor():
    checker = AsyncChecker()
    assert isinstance(checker, Checker)


@pytest.mark.parametrize("raises", [None, KeyboardInterrupt, Exception])
def test_asynchecker_run(patches, raises):
    checker = AsyncChecker()

    patched = patches(
        "asyncio",
        "Checker.exit",
        ("AsyncChecker._run", dict(new_callable=MagicMock)),
        ("AsyncChecker.on_checks_complete", dict(new_callable=MagicMock)),
        prefix="tools.base.checker")

    with patched as (m_async, m_exit, m_run, m_complete):
        if raises:
            m_run.side_effect = raises

            if raises == KeyboardInterrupt:
                result = checker.run()
            else:
                with pytest.raises(raises):
                    checker.run()
                return
        else:
            assert (
                checker.run()
                == m_async.get_event_loop.return_value.run_until_complete.return_value)

    if raises == KeyboardInterrupt:
        assert (
            list(m_exit.call_args)
            == [(), {}])
        assert (
            list(m_async.get_event_loop.call_args_list[1])
            == [(), {}])
        assert (
            list(m_async.get_event_loop.return_value.run_until_complete.call_args)
            == [(m_complete.return_value,), {}])
        assert (
            list(m_complete.call_args)
            == [(), {}])
        assert result == m_async.get_event_loop.return_value.run_until_complete.return_value
        return

    assert not m_exit.called
    assert (
        list(m_async.get_event_loop.call_args)
        == [(), {}])
    assert (
        list(m_async.get_event_loop.return_value.run_until_complete.call_args)
        == [(m_run.return_value,), {}])
    assert (
        list(m_run.call_args)
        == [(), {}])


@pytest.mark.asyncio
async def test_asynchecker_on_check_begin(patches):
    checker = AsyncChecker()
    patched = patches(
        "Checker.on_check_begin",
        prefix="tools.base.checker")

    with patched as (m_super, ):
        assert not await checker.on_check_begin("CHECKNAME")

    assert (
        list(m_super.call_args)
        == [('CHECKNAME',), {}])


@pytest.mark.asyncio
async def test_asynchecker_on_check_run(patches):
    checker = AsyncChecker()
    patched = patches(
        "Checker.on_check_run",
        prefix="tools.base.checker")

    with patched as (m_super, ):
        assert not await checker.on_check_run("CHECKNAME")

    assert (
        list(m_super.call_args)
        == [('CHECKNAME',), {}])


@pytest.mark.asyncio
async def test_asynchecker_on_checks_begin(patches):
    checker = AsyncChecker()
    patched = patches(
        "Checker.on_checks_begin",
        prefix="tools.base.checker")

    with patched as (m_super, ):
        assert not await checker.on_checks_begin()

    assert (
        list(m_super.call_args)
        == [(), {}])


@pytest.mark.asyncio
async def test_asynchecker_on_checks_complete(patches):
    checker = AsyncChecker()

    patched = patches(
        "Checker.on_checks_complete",
        prefix="tools.base.checker")

    with patched as (m_complete, ):
        assert (
            await checker.on_checks_complete()
            == m_complete.return_value)

    assert (
        list(m_complete.call_args)
        == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [True, False])
@pytest.mark.parametrize("exiting", [True, False])
async def test_asynchecker__run(patches, raises, exiting):
    _check1 = MagicMock()
    _check2 = MagicMock()
    _check3 = MagicMock()

    class AsyncCheckerWithChecks(AsyncChecker):

        async def check_check1(self):
            return _check1()

        async def check_check2(self):
            return _check2()

        async def check_check3(self):
            return _check3()

    class SomeError(Exception):
        pass

    checker = AsyncCheckerWithChecks()

    patched = patches(
        "Checker.log",
        "Checker.get_checks",
        "AsyncChecker.on_checks_begin",
        "AsyncChecker.on_check_begin",
        "AsyncChecker.on_check_run",
        "AsyncChecker.on_checks_complete",
        ("AsyncChecker.exiting", dict(new_callable=PropertyMock)),
        prefix="tools.base.checker")

    with patched as (m_log, m_checks, m_begin, m_check, m_run, m_complete, m_exit):
        m_checks.return_value = ["check1", "check2", "check3"]
        m_exit.return_value = exiting
        if raises:
            m_begin.side_effect = SomeError("AN ERROR OCCURRED")

            with pytest.raises(SomeError):
                await checker._run()
        elif exiting:
            assert await checker._run() == 1
        else:
            assert await checker._run() == m_complete.return_value

    assert (
        list(m_begin.call_args)
        == [(), {}])

    if exiting:
        return

    assert (
        list(m_complete.call_args)
        == [(), {}])

    if raises:
        return

    assert (
        list(m_checks.call_args)
        == [(), {}])
    assert (
        list(list(c) for c in m_check.call_args_list)
        == [[(f'check{i}',), {}] for i in range(1, 4)])
    for check in [_check1, _check2, _check3]:
        assert (
            list(check.call_args)
            == [(), {}])
    assert (
        list(list(c) for c in m_run.call_args_list)
        == [[('check1',), {}], [('check2',), {}], [('check3',), {}]])
