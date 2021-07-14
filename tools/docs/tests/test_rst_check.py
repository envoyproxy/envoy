
import types
from unittest.mock import MagicMock, PropertyMock

import pytest

from tools.docs import rst_check


def test_rst_check_current_version_constructor():
    version_file = rst_check.CurrentVersionFile("PATH")
    assert version_file._path == "PATH"
    assert version_file.path == "PATH"


def test_rst_check_current_version_lines(patches):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        "open",
        ("CurrentVersionFile.path", dict(new_callable=PropertyMock)),
        prefix="tools.docs.rst_check")

    expected = [MagicMock(), MagicMock()]
    with patched as (m_open, m_path):
        m_open.return_value.__enter__.return_value.readlines.return_value = expected
        _lines = version_file.lines
        assert isinstance(_lines, types.GeneratorType)
        lines = list(_lines)

    assert (
        list(m_open.call_args)
        == [(m_path.return_value,), {}])
    assert lines == [expected[0].strip.return_value, expected[1].strip.return_value]


@pytest.mark.parametrize(
    "prior", [
        [".", True],
        ["asdf .", True],
        ["asdf.", True],
        ["asdf", False],
        ["asdf,", False],
        ["", True],
        ["foo. <asdf>`", True],
        ["foo. <asdf>` xxx", False],
        ["foo <asdf>`", False]])
def test_rst_check_current_version_prior_ends_with_period(prior):
    version_file = rst_check.CurrentVersionFile("PATH")
    version_file.prior_line, expected = prior
    assert version_file.prior_endswith_period == expected


@pytest.mark.parametrize("matches", [True, False, "partial"])
def test_rst_check_current_version_check_flags(patches, matches):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        "RELOADABLE_FLAG_REGEX",
        prefix="tools.docs.rst_check")

    with patched as (m_flag, ):
        if matches == "partial":
            m_flag.match.return_value.groups.return_value.__getitem__.return_value.startswith.return_value = False
        elif not matches:
            m_flag.match.return_value = False
        result = version_file.check_flags("LINE")

    assert (
        list(m_flag.match.call_args)
        == [('LINE',), {}])

    if matches:
        assert (
            list(m_flag.match.return_value.groups.call_args)
            == [(), {}])
        assert (
            list(m_flag.match.return_value.groups.return_value.__getitem__.return_value.startswith.call_args)
            == [(' ``',), {}])
        if matches == "partial":
            assert (
                result
                == [f"Flag {m_flag.match.return_value.groups.return_value.__getitem__.return_value} should be enclosed in double back ticks"])
            assert (
                list(list(c) for c in m_flag.match.return_value.groups.return_value.__getitem__.call_args_list)
                == [[(0,), {}], [(1,), {}]])
        else:
            assert (
                list(list(c) for c in m_flag.match.return_value.groups.return_value.__getitem__.call_args_list)
                == [[(0,), {}]])
            assert result == []
    else:
        assert result == []


@pytest.mark.parametrize("line", ["", " ", "* ", "*asdf"])
@pytest.mark.parametrize("prior_period", [True, False])
@pytest.mark.parametrize("prior_line", ["", "line_content"])
def test_rst_check_current_version_check_line(patches, line, prior_period, prior_line):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        "CurrentVersionFile.check_reflink",
        "CurrentVersionFile.check_flags",
        "CurrentVersionFile.check_list_item",
        "CurrentVersionFile.check_previous_period",
        prefix="tools.docs.rst_check")
    version_file.prior_line = prior_line

    with patched as (m_ref, m_flags, m_item, m_period):
        result = version_file.check_line(line)

    expected = m_ref.return_value.__add__.return_value
    assert (
        list(m_ref.call_args)
        == [(line,), {}])
    assert (
        list(m_flags.call_args)
        == [(line,), {}])
    assert (
        list(m_ref.return_value.__add__.call_args)
        == [(m_flags.return_value,), {}])

    if line.startswith("* "):
        assert (
            list(expected.__iadd__.call_args)
            == [(m_item.return_value,), {}])
        assert (
            list(m_item.call_args)
            == [(line,), {}])
        assert not m_period.called
        assert result == expected.__iadd__.return_value
        assert version_file.prior_line == prior_line
    elif not line:
        assert (
            list(expected.__iadd__.call_args)
            == [(m_period.return_value,), {}])
        assert (
            list(m_period.call_args)
            == [(), {}])
        assert result == expected.__iadd__.return_value
        assert not m_item.called
        assert version_file.prior_line == ""
    elif prior_line:
        assert not m_period.called
        assert not m_item.called
        assert version_file.prior_line == prior_line + line
        assert result == expected
    else:
        assert not m_period.called
        assert not m_item.called
        assert version_file.prior_line == prior_line
        assert result == expected


@pytest.mark.parametrize("prior", [True, False])
@pytest.mark.parametrize("matches", [True, False])
@pytest.mark.parametrize("prior_first", ["", "AAA", "BBB", "CCC"])
@pytest.mark.parametrize("prior_next", ["", "AAA", "BBB", "CCC"])
@pytest.mark.parametrize("first_word", ["AAA", "BBB", "CCC"])
@pytest.mark.parametrize("next_word", ["AAA", "BBB", "CCC"])
def test_rst_check_current_version_check_list_item(patches, matches, prior, prior_first, prior_next, first_word, next_word):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        "VERSION_HISTORY_NEW_LINE_REGEX",
        "CurrentVersionFile.set_tokens",
        ("CurrentVersionFile.prior_endswith_period", dict(new_callable=PropertyMock)),
        prefix="tools.docs.rst_check")
    version_file.prior_line = "PRIOR LINE"
    version_file.first_word_of_prior_line = prior_first
    version_file.next_word_to_check = prior_next

    def _get_item(item):
        if item == 0:
            return first_word
        return next_word

    with patched as (m_regex, m_tokens, m_prior):
        if not matches:
            m_regex.match.return_value = False
        else:
            m_regex.match.return_value.groups.return_value.__getitem__.side_effect = _get_item
        m_prior.return_value = prior
        result = version_file.check_list_item("LINE")

    expected = []
    if not prior:
        expected += ["The following release note does not end with a '.'\n PRIOR LINE"]

    assert (
        list(m_regex.match.call_args)
        == [('LINE',), {}])

    if not matches:
        expected += [
            f"Version history line malformed. "
            f"Does not match VERSION_HISTORY_NEW_LINE_REGEX in docs_check.py\n LINE\n"
            "Please use messages in the form 'category: feature explanation.', "
            "starting with a lower-cased letter and ending with a period."]
        assert result == expected
        assert not m_tokens.called
        return

    assert (
        list(list(c) for c in m_regex.match.return_value.groups.call_args_list)
        == [[(), {}], [(), {}]])

    if prior_first and prior_first > first_word:
        expected += [f'Version history not in alphabetical order ({prior_first} vs {first_word}): please check placement of line\n LINE. ']

    if prior_first == first_word and prior_next > next_word:
        expected += [f'Version history not in alphabetical order ({prior_next} vs {next_word}): please check placement of line\n LINE. ']

    assert result == expected
    assert (
        list(m_tokens.call_args)
        == [('LINE', first_word, next_word), {}])


@pytest.mark.parametrize("prior", [True, False])
def test_rst_check_current_version_check_previous_period(patches, prior):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        ("CurrentVersionFile.prior_endswith_period", dict(new_callable=PropertyMock)),
        prefix="tools.docs.rst_check")

    version_file.prior_line = "PRIOR"

    with patched as (m_period, ):
        m_period.return_value = prior
        result = version_file.check_previous_period()
    if prior:
        assert result == []
    else:
        assert result == ["The following release note does not end with a '.'\n PRIOR"]


@pytest.mark.parametrize("matches", [True, False])
def test_rst_check_current_version_check_reflink(patches, matches):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        "INVALID_REFLINK",
        prefix="tools.docs.rst_check")

    with patched as (m_reflink, ):
        m_reflink.match.return_value = matches
        result = version_file.check_reflink("LINE")

    assert (
        list(m_reflink.match.call_args)
        == [('LINE',), {}])

    if matches:
        assert (
            result
            == ['Found text " ref:". This should probably be " :ref:"\nLINE'])
    else:
        assert result == []


@pytest.mark.parametrize(
    "lines",
    [[],
     [[0, "AAA"], [1, "BBB"]],
     [[0, "AAA"], [1, "BBB"], [2, "CCC"]],
     [[0, "AAA"], [1, "Deprecated"], [2, "BBB"], [3, "CCC"]]])
@pytest.mark.parametrize("errors", [[], ["err1", "err2"]])
@pytest.mark.parametrize("matches", [True, False])
def test_rst_check_current_version_run_checks(patches, lines, errors, matches):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        "enumerate",
        "VERSION_HISTORY_SECTION_NAME",
        "CurrentVersionFile.set_tokens",
        "CurrentVersionFile.check_line",
        ("CurrentVersionFile.lines", dict(new_callable=PropertyMock)),
        prefix="tools.docs.rst_check")

    with patched as (m_enum, m_section, m_tokens, m_check, m_lines):
        m_enum.return_value = lines
        m_check.return_value = errors
        m_section.match.return_value = matches
        _result = version_file.run_checks()
        assert isinstance(_result, types.GeneratorType)
        result = list(_result)

    assert (
        list(m_enum.call_args)
        == [(m_lines.return_value,), {}])

    if not lines:
        assert result == []
        assert not m_section.match.called
        assert not m_check.called
        assert (
            list(list(c) for c in m_tokens.call_args_list)
            == [[(), {}]])
        return

    _match = []
    _tokens = 1
    _checks = []
    _errors = []

    for line_number, line in lines:
        _match.append(line)
        if matches:
            if line == "Deprecated":
                break
            _tokens += 1
        _checks.append(line)
        for error in errors:
            _errors.append((line_number, error))
    assert (
        list(list(c) for c in m_section.match.call_args_list)
        == [[(line,), {}] for line in _match])
    assert (
        list(list(c) for c in m_tokens.call_args_list)
        == [[(), {}]] * _tokens)
    assert (
        list(list(c) for c in m_check.call_args_list)
        == [[(line,), {}] for line in _checks])
    assert (
        result
        == [f"(PATH:{line_number + 1}) {error}"
            for line_number, error in _errors])


@pytest.mark.parametrize("line", [None, "", "foo"])
@pytest.mark.parametrize("first_word", [None, "", "foo"])
@pytest.mark.parametrize("next_word", [None, "", "foo"])
def test_rst_check_current_version_set_tokens(patches, line, first_word, next_word):
    version_file = rst_check.CurrentVersionFile("PATH")
    version_file.set_tokens(line, first_word, next_word)
    assert version_file.first_word_of_prior_line == first_word
    assert version_file.next_word_to_check == next_word


def test_rst_checker_constructor():
    checker = rst_check.RSTChecker("path1", "path2", "path3")
    assert checker.checks == ("current_version", )
    assert checker.args.paths == ['path1', 'path2', 'path3']


@pytest.mark.parametrize("errors", [[], ["err1", "err2"]])
def test_rst_checker_check_current_version(patches, errors):
    checker = rst_check.RSTChecker("path1", "path2", "path3")

    patched = patches(
        "CurrentVersionFile",
        "RSTChecker.error",
        prefix="tools.docs.rst_check")

    with patched as (m_version, m_error):
        m_version.return_value.run_checks.return_value = errors
        checker.check_current_version()

    assert (
        list(m_version.call_args)
        == [('docs/root/version_history/current.rst',), {}])
    assert (
        list(m_version.return_value.run_checks.call_args)
        == [(), {}])

    if not errors:
        assert not m_error.called
    else:
        assert (
            list(m_error.call_args)
            == [('current_version', ['err1', 'err2']), {}])


def test_rst_checker_main(command_main):
    command_main(
        rst_check.main,
        "tools.docs.rst_check.RSTChecker")
