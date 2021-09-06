import types
from contextlib import contextmanager
from unittest.mock import AsyncMock, patch, MagicMock, PropertyMock

import pytest

from tools.code_format import python_check


def test_python_checker_constructor():
    checker = python_check.PythonChecker("path1", "path2", "path3")
    assert checker.checks == ("flake8", "yapf")
    assert checker.args.paths == ['path1', 'path2', 'path3']


@pytest.mark.parametrize("diff_path", ["", None, "PATH"])
def test_python_diff_path(patches, diff_path):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        "pathlib",
        ("PythonChecker.args", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_plib, m_args):
        m_args.return_value.diff_file = diff_path
        assert checker.diff_file_path == (m_plib.Path.return_value if diff_path else None)

    if diff_path:
        assert (
            list(m_plib.Path.call_args)
            == [(m_args.return_value.diff_file, ), {}])
    else:
        assert not m_plib.Path.called


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
            == ('--config',
                str(m_flake8_config.return_value),
                str(m_path.return_value)))


def test_python_flake8_config_path(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        ("PythonChecker.path", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_path, ):
        assert checker.flake8_config_path == m_path.return_value.joinpath.return_value

    assert (
        list(m_path.return_value.joinpath.call_args)
        == [(python_check.FLAKE8_CONFIG, ), {}])


def test_python_yapf_config_path(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        ("PythonChecker.path", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_path, ):
        assert checker.yapf_config_path == m_path.return_value.joinpath.return_value

    assert (
        list(m_path.return_value.joinpath.call_args)
        == [(python_check.YAPF_CONFIG, ), {}])


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
        == [(str(m_path.return_value),), {}])


def test_python_add_arguments(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    add_mock = patch("tools.code_format.python_check.checker.AsyncChecker.add_arguments")
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


@pytest.mark.asyncio
@pytest.mark.parametrize("errors", [[], ["err1", "err2"]])
async def test_python_check_flake8(patches, errors):
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
        assert not await checker.check_flake8()

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


@pytest.mark.asyncio
async def test_python_check_yapf(patches):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        "aio",
        ("PythonChecker.yapf_format", dict(new_callable=MagicMock)),
        "PythonChecker.yapf_result",
        ("PythonChecker.yapf_files", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")
    files = ["file1", "file2", "file3"]

    async def concurrent(iters):
        assert isinstance(iters, types.GeneratorType)
        for i, format_result in enumerate(iters):
            yield (format_result, (f"REFORMAT{i}", f"ENCODING{i}", f"CHANGED{i}"))

    with patched as (m_aio, m_yapf_format, m_yapf_result, m_yapf_files):
        m_yapf_files.return_value = files
        m_aio.concurrent.side_effect = concurrent
        assert not await checker.check_yapf()

    assert (
        list(list(c) for c in m_yapf_format.call_args_list)
        == [[(file,), {}] for file in files])
    assert (
        list(list(c) for c in m_yapf_result.call_args_list)
        == [[(m_yapf_format.return_value, f"REFORMAT{i}", f"CHANGED{i}"), {}] for i, _ in enumerate(files)])


@pytest.mark.asyncio
@pytest.mark.parametrize("errors", [[], ["check2", "check3"], ["check1", "check3"]])
@pytest.mark.parametrize("warnings", [[], ["check4", "check5"], ["check1", "check5"]])
async def test_python_on_check_run(patches, errors, warnings):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    checkname = "check1"
    patched = patches(
        "PythonChecker.succeed",
        ("PythonChecker.name", dict(new_callable=PropertyMock)),
        ("PythonChecker.failed", dict(new_callable=PropertyMock)),
        ("PythonChecker.warned", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_succeed, m_name, m_failed, m_warned):
        m_failed.return_value = errors
        m_warned.return_value = warnings
        assert not await checker.on_check_run(checkname)

    if checkname in warnings or checkname in errors:
        assert not m_succeed.called
    else:
        assert (
            list(m_succeed.call_args)
            == [(checkname, [checkname]), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("diff_path", ["", "DIFF1"])
@pytest.mark.parametrize("failed", [True, False])
async def test_python_on_checks_complete(patches, diff_path, failed):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        "aio",
        ("checker.AsyncChecker.on_checks_complete", dict(new_callable=AsyncMock)),
        ("PythonChecker.diff_file_path", dict(new_callable=PropertyMock)),
        ("PythonChecker.has_failed", dict(new_callable=PropertyMock)),
        ("PythonChecker.path", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_aio, m_super, m_diff, m_failed, m_path):
        m_aio.async_subprocess.run = AsyncMock()
        if not diff_path:
            m_diff.return_value = None
        m_failed.return_value = failed
        assert await checker.on_checks_complete() == m_super.return_value

    if diff_path and failed:
        assert (
            list(m_aio.async_subprocess.run.call_args)
            == [(['git', 'diff', 'HEAD'],),
                dict(capture_output=True, cwd=m_path.return_value)])
        assert (
            list(m_diff.return_value.write_bytes.call_args)
            == [(m_aio.async_subprocess.run.return_value.stdout,), {}])
    else:
        assert not m_aio.async_subprocess.run.called

    assert (
        list(m_super.call_args)
        == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("fix", [True, False])
async def test_python_yapf_format(patches, fix):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        "yapf.yapf_api.FormatFile",
        ("PythonChecker.yapf_config_path", dict(new_callable=PropertyMock)),
        ("PythonChecker.fix", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_format, m_config, m_fix):
        m_fix.return_value = fix
        assert await checker.yapf_format("FILENAME") == ("FILENAME", m_format.return_value)

    assert (
        list(m_format.call_args)
        == [('FILENAME',),
            {'style_config': str(m_config.return_value),
             'in_place': fix,
             'print_diff': not fix}])
    assert (
        list(list(c) for c in m_fix.call_args_list)
        == [[(), {}], [(), {}]])


@pytest.mark.parametrize("reformatted", ["", "REFORMAT"])
@pytest.mark.parametrize("fix", [True, False])
@pytest.mark.parametrize("changed", [True, False])
def test_python_yapf_result(patches, reformatted, fix, changed):
    checker = python_check.PythonChecker("path1", "path2", "path3")
    patched = patches(
        "PythonChecker.succeed",
        "PythonChecker.warn",
        "PythonChecker.error",
        ("PythonChecker.fix", dict(new_callable=PropertyMock)),
        prefix="tools.code_format.python_check")

    with patched as (m_succeed, m_warn, m_error, m_fix):
        m_fix.return_value = fix
        checker.yapf_result("FILENAME", reformatted, changed)

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
    if reformatted:
        assert not m_error.called
        assert len(m_warn.call_args_list) == 1
        assert (
            list(m_warn.call_args)
            == [('yapf', [f'FILENAME: diff\n{reformatted}']), {}])
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
