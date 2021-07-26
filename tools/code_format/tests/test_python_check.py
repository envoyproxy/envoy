from contextlib import contextmanager
from unittest.mock import patch, MagicMock, PropertyMock

import pytest

from tools.code_format import python_check


def test_python_checker_constructor():
    checker = python_check.PythonChecker("path1", "path2", "path3")
    assert checker.checks == ("flake8", "yapf")
    assert checker.args.paths == ['path1', 'path2', 'path3']


def test_python_diff_path():
    checker = python_check.PythonChecker("path1", "path2", "path3")
    args_mock = patch("tools.code_format.python_check.PythonChecker.args", new_callable=PropertyMock)

    with args_mock as m_args:
        assert checker.diff_file_path == m_args.return_value.diff_file


def test_python_flake8_app(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        ("PythonChecker.flake8_args", dict(new_callable=PropertyMock)),
        "Flake8Application",
        prefix="tools.code_format.python_check")

    with patched as (m_flake8_args, m_flake8_app):
        assert checker.flake8_app == m_flake8_app.return_value

    assert (
        list(m_flake8_app.call_args)
        == [(), {}])
    assert (
        list(m_flake8_app.return_value.initialize.call_args)
        == [(m_flake8_args.return_value,), {}])


def test_python_flake8_args(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        ("PythonChecker.flake8_config_path", dict(new_callable=PropertyMock)),
        ("PythonChecker.path", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_flake8_config, m_path):
        assert (
            checker.flake8_args
            == ['--config',
                m_flake8_config.return_value, m_path.return_value])


def test_python_flake8_config_path(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        ("PythonChecker.path", dict(new_callable=PropertyMock)),
        "os.path.join",
        prefix="tools.code_format.python_check")

    with patched as (m_path, m_join):
        assert checker.flake8_config_path == m_join.return_value

    assert (
        list(m_join.call_args)
        == [(m_path.return_value, python_check.FLAKE8_CONFIG), {}])


def test_python_yapf_config_path(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        ("PythonChecker.path", dict(new_callable=PropertyMock)),
        "os.path.join",
        prefix="tools.code_format.python_check")

    with patched as (m_path, m_join):
        assert checker.yapf_config_path == m_join.return_value

    assert (
        list(m_join.call_args)
        == [(m_path.return_value, python_check.YAPF_CONFIG), {}])


def test_python_yapf_files(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")

    patched = patches(
        ("PythonChecker.args", dict(new_callable=PropertyMock)),
        ("PythonChecker.path", dict(new_callable=PropertyMock)),
        "yapf.file_resources.GetCommandLineFiles",
        "yapf.file_resources.GetExcludePatternsForDir",
        prefix="tools.code_format.python_check")

    with patched as (m_args, m_path, m_yapf_files, m_yapf_exclude):
        assert checker.yapf_files == m_yapf_files.return_value

    assert (
        list(m_yapf_files.call_args)
        == [(m_args.return_value.paths,),
            {'recursive': m_args.return_value.recurse,
             'exclude': m_yapf_exclude.return_value}])
    assert (
        list(m_yapf_exclude.call_args)
        == [(m_path.return_value,), {}])


def test_python_add_arguments(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    add_mock = patch("tools.code_format.python_check.checker.ForkingChecker.add_arguments")
    m_parser = MagicMock()

    with add_mock as m_add:
        checker.add_arguments(m_parser)

    assert (
        list(m_add.call_args)
        == [(m_parser,), {}])
    assert (
        list(list(c) for c in m_parser.add_argument.call_args_list)
        == [[('--recurse', '-r'),
             {'choices': ['yes', 'no'],
              'default': 'yes',
              'help': 'Recurse path or paths directories'}],
            [('--diff-file',),
             {'default': None, 'help': 'Specify the path to a diff file with fixes'}]])


@pytest.mark.parametrize("errors", [[], ["err1", "err2"]])
def test_python_check_flake8(patches, errors):
    checker = python_check.PythonChecker("path1", "path2", "path3")

    patched = patches(
        "utils.buffered",
        "PythonChecker.error",
        "PythonChecker._strip_lines",
        ("PythonChecker.flake8_app", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    @contextmanager
    def mock_buffered(stdout=None, mangle=None):
        yield
        stdout.extend(errors)

    with patched as (m_buffered, m_error, m_mangle, m_flake8_app):
        m_buffered.side_effect = mock_buffered
        checker.check_flake8()

    assert (
        list(m_buffered.call_args)
        == [(), {'stdout': errors, 'mangle': m_mangle}])
    assert (
        list(m_flake8_app.return_value.run_checks.call_args)
        == [(), {}])
    assert (
        list(m_flake8_app.return_value.report.call_args)
        == [(), {}])

    if errors:
        assert (
            list(m_error.call_args)
            == [('flake8', ['err1', 'err2']), {}])
    else:
        assert not m_error.called


def test_python_check_recurse():
    checker = python_check.PythonChecker("path1", "path2", "path3")
    args_mock = patch(
        "tools.code_format.python_check.PythonChecker.args",
        new_callable=PropertyMock)

    with args_mock as m_args:
        assert checker.recurse == m_args.return_value.recurse
    assert "recurse" not in checker.__dict__


def test_python_check_yapf(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        "PythonChecker.yapf_run",
        ("PythonChecker.yapf_files", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_yapf_run, m_yapf_files):
        m_yapf_files.return_value = ["file1", "file2", "file3"]
        checker.check_yapf()

    assert (
        list(list(c) for c in m_yapf_files.call_args_list)
        == [[(), {}]])
    assert (
        list(list(c) for c in m_yapf_run.call_args_list)
        == [[('file1',), {}], [('file2',), {}], [('file3',), {}]])


TEST_CHECK_RESULTS = (
    ("check1", [], []),
    ("check1", ["check2", "check3"], ["check4", "check5"]),
    ("check1", ["check1", "check3"], ["check4", "check5"]),
    ("check1", ["check2", "check3"], ["check1", "check5"]),
    ("check1", ["check1", "check3"], ["check1", "check5"]))


@pytest.mark.parametrize("results", TEST_CHECK_RESULTS)
def test_python_on_check_run(patches, results):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    checkname, errors, warnings = results
    patched = patches(
        "PythonChecker.succeed",
        ("PythonChecker.name", dict(new_callable=PropertyMock)),
        ("PythonChecker.failed", dict(new_callable=PropertyMock)),
        ("PythonChecker.warned", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_succeed, m_name, m_failed, m_warned):
        m_failed.return_value = errors
        m_warned.return_value = warnings
        checker.on_check_run(checkname)

    if checkname in warnings or checkname in errors:
        assert not m_succeed.called
    else:
        assert (
            list(m_succeed.call_args)
            == [(checkname, [checkname]), {}])


TEST_CHECKS_COMPLETE = (
    ("DIFF1", False),
    ("DIFF1", True),
    ("", False),
    ("", True))


@pytest.mark.parametrize("results", TEST_CHECKS_COMPLETE)
def test_python_on_checks_complete(patches, results):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    diff_path, failed = results
    patched = patches(
        "open",
        "checker.ForkingChecker.subproc_run",
        "checker.Checker.on_checks_complete",
        ("PythonChecker.diff_file_path", dict(new_callable=PropertyMock)),
        ("PythonChecker.has_failed", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_open, m_fork, m_super, m_diff, m_failed):
        m_diff.return_value = diff_path
        m_failed.return_value = failed
        assert checker.on_checks_complete() == m_super.return_value

    if diff_path and failed:
        assert (
            list(m_fork.call_args)
            == [(['git', 'diff', 'HEAD'],), {}])
        assert (
            list(m_open.call_args)
            == [(diff_path, 'wb'), {}])
        assert (
            list(m_open.return_value.__enter__.return_value.write.call_args)
            == [(m_fork.return_value.stdout,), {}])
    else:
        assert not m_fork.called
        assert not m_open.called

    assert (
        list(m_super.call_args)
        == [(), {}])


@pytest.mark.parametrize("fix", [True, False])
def test_python_yapf_format(patches, fix):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        "yapf.yapf_api.FormatFile",
        ("PythonChecker.yapf_config_path", dict(new_callable=PropertyMock)),
        ("PythonChecker.fix", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_format, m_config, m_fix):
        m_fix.return_value = fix
        assert checker.yapf_format("FILENAME") == m_format.return_value

    assert (
        list(m_format.call_args)
        == [('FILENAME',),
            {'style_config': m_config.return_value,
             'in_place': fix,
             'print_diff': not fix}])
    assert (
        list(list(c) for c in m_fix.call_args_list)
        == [[(), {}], [(), {}]])


TEST_FORMAT_RESULTS = (
    ("", "", True),
    ("", "", False),
    ("REFORMAT", "", True),
    ("REFORMAT", "", False))


@pytest.mark.parametrize("format_results", TEST_FORMAT_RESULTS)
@pytest.mark.parametrize("fix", [True, False])
def test_python_yapf_run(patches, fix, format_results):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    reformat, encoding, changed = format_results
    patched = patches(
        "PythonChecker.yapf_format",
        "PythonChecker.succeed",
        "PythonChecker.warn",
        "PythonChecker.error",
        ("PythonChecker.fix", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_format, m_succeed, m_warn, m_error, m_fix):
        m_fix.return_value = fix
        m_format.return_value = format_results
        checker.yapf_run("FILENAME")

    if not changed:
        assert (
            list(m_succeed.call_args)
            == [('yapf', ['FILENAME']), {}])
        assert not m_warn.called
        assert not m_error.called
        assert not m_fix.called
        return
    assert not m_succeed.called
    if fix:
        assert not m_error.called
        assert len(m_warn.call_args_list) == 1
        assert (
            list(m_warn.call_args)
            == [('yapf', [f'FILENAME: reformatted']), {}])
        return
    if reformat:
        assert not m_error.called
        assert len(m_warn.call_args_list) == 1
        assert (
            list(m_warn.call_args)
            == [('yapf', [f'FILENAME: diff\n{reformat}']), {}])
        return
    assert not m_warn.called
    assert (
        list(m_error.call_args)
        == [('yapf', ['FILENAME']), {}])


def test_python_strip_lines():
    checker = python_check.PythonChecker("path1", "path2", "path3")
    strip_mock = patch("tools.code_format.python_check.PythonChecker._strip_line")
    lines = ["", "foo", "", "bar", "", "", "baz", "", ""]

    with strip_mock as m_strip:
        assert (
            checker._strip_lines(lines)
            == [m_strip.return_value] * 3)

    assert (
        list(list(c) for c in m_strip.call_args_list)
        == [[('foo',), {}], [('bar',), {}], [('baz',), {}]])


@pytest.mark.parametrize("line", ["REMOVE/foo", "REMOVE", "bar", "other", "REMOVE/baz", "baz"])
def test_python_strip_line(line):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    path_mock = patch(
        "tools.code_format.python_check.PythonChecker.path",
        new_callable=PropertyMock)

    with path_mock as m_path:
        m_path.return_value = "REMOVE"
        assert (
            checker._strip_line(line)
            == line[7:] if line.startswith(f"REMOVE/") else line)


def test_python_checker_main(command_main):
    command_main(
        python_check.main,
        "tools.code_format.python_check.PythonChecker")
