
import types
from unittest.mock import MagicMock, PropertyMock

import pytest

from tools.docs import rst_check


def test_rst_check_current_version_constructor():
    version_file = rst_check.CurrentVersionFile("PATH")
    assert version_file._path == "PATH"
    assert version_file.path == "PATH"


@pytest.mark.parametrize(
    "constant",
    (("single_tick_re", "SINGLE_TICK_REGEX"),
     ("ref_ticks_re", "REF_TICKS_REGEX"),
     ("link_ticks_re", "LINK_TICKS_REGEX"),
     ("invalid_reflink_re", "INVALID_REFLINK"),
     ("new_line_re", "VERSION_HISTORY_NEW_LINE_REGEX"),
     ("punctuation_re", "REF_WITH_PUNCTUATION_REGEX"),
     ("section_name_re", "VERSION_HISTORY_SECTION_NAME")))
def test_rst_check_current_version_regexes(patches, constant):
    version_file = rst_check.CurrentVersionFile("PATH")
    prop, constant = constant
    patched = patches(
        "re",
        prefix="tools.docs.rst_check")

    with patched as (m_re, ):
        assert getattr(version_file, prop) == m_re.compile.return_value

    assert (
        list(m_re.compile.call_args)
        == [(getattr(rst_check, constant),), {}])
    assert prop in version_file.__dict__


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


@pytest.mark.parametrize("line", ["", " ", "* ", "*asdf"])
@pytest.mark.parametrize("prior_period", [True, False])
@pytest.mark.parametrize("prior_line", ["", "line_content"])
def test_rst_check_current_version_check_line(patches, line, prior_period, prior_line):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        "CurrentVersionFile.check_reflink",
        "CurrentVersionFile.check_list_item",
        "CurrentVersionFile.check_previous_period",
        "CurrentVersionFile.check_ticks",
        prefix="tools.docs.rst_check")
    version_file.prior_line = prior_line

    with patched as (m_ref, m_item, m_period, m_ticks):
        result = version_file.check_line(line)

    expected = m_ref.return_value.__add__.return_value

    assert (
        list(m_ref.call_args)
        == [(line,), {}])
    assert (
        list(m_ticks.call_args)
        == [(line,), {}])
    assert (
        list(m_ref.return_value.__add__.call_args)
        == [(m_ticks.return_value,), {}])

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
        "CurrentVersionFile.set_tokens",
        ("CurrentVersionFile.prior_endswith_period", dict(new_callable=PropertyMock)),
        ("CurrentVersionFile.new_line_re", dict(new_callable=PropertyMock)),
        prefix="tools.docs.rst_check")
    version_file.prior_line = "PRIOR LINE"
    version_file.first_word_of_prior_line = prior_first
    version_file.next_word_to_check = prior_next

    def _get_item(item):
        if item == 0:
            return first_word
        return next_word

    with patched as (m_tokens, m_prior, m_regex):
        if not matches:
            m_regex.return_value.match.return_value = False
        else:
            m_regex.return_value.match.return_value.groups.return_value.__getitem__.side_effect = _get_item
        m_prior.return_value = prior
        result = version_file.check_list_item("LINE")

    expected = []
    if not prior:
        expected += ["The following release note does not end with a '.'\n PRIOR LINE"]

    assert (
        list(m_regex.return_value.match.call_args)
        == [('LINE',), {}])

    if not matches:
        expected += [
            f"Version history line malformed. "
            f"Does not match VERSION_HISTORY_NEW_LINE_REGEX\n LINE\n"
            "Please use messages in the form 'category: feature explanation.', "
            "starting with a lower-cased letter and ending with a period."]
        assert result == expected
        assert not m_tokens.called
        return

    assert (
        list(list(c) for c in m_regex.return_value.match.return_value.groups.call_args_list)
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
        ("CurrentVersionFile.invalid_reflink_re", dict(new_callable=PropertyMock)),
        prefix="tools.docs.rst_check")

    with patched as (m_reflink, ):
        m_reflink.return_value.match.return_value = matches
        result = version_file.check_reflink("LINE")

    assert (
        list(m_reflink.return_value.match.call_args)
        == [('LINE',), {}])

    if matches:
        assert (
            result
            == ['Found text " ref:". This should probably be " :ref:"\nLINE'])
    else:
        assert result == []


@pytest.mark.parametrize("single_tick_re_matches", [True, False])
@pytest.mark.parametrize("ref_ticks_re", [True, False])
@pytest.mark.parametrize("link_ticks_re", [True, False])
def test_rst_check_current_version_check_ticks(patches, single_tick_re_matches, ref_ticks_re, link_ticks_re):
    version_file = rst_check.CurrentVersionFile("PATH")
    patched = patches(
        ("CurrentVersionFile.single_tick_re", dict(new_callable=PropertyMock)),
        ("CurrentVersionFile.ref_ticks_re", dict(new_callable=PropertyMock)),
        ("CurrentVersionFile.link_ticks_re", dict(new_callable=PropertyMock)),
        prefix="tools.docs.rst_check")

    with patched as (m_single_tick_re, m_ref_tickes_re, m_link_ticks_re):
        m_single_tick_re.return_value.match.return_value = single_tick_re_matches
        m_ref_tickes_re.return_value.match.return_value = ref_ticks_re
        m_link_ticks_re.return_value.match.return_value = link_ticks_re
        assert (
            version_file.check_ticks("LINE")
            == (["Backticks should come in pairs (``foo``) except for links (`title <url>`_) or refs (ref:`text <ref>`): LINE"]
                if (single_tick_re_matches and (not ref_ticks_re) and (not link_ticks_re)) else []))
    assert (
        list(m_single_tick_re.return_value.match.call_args)
        == [('LINE',), {}])


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
        "CurrentVersionFile.set_tokens",
        "CurrentVersionFile.check_line",
        ("CurrentVersionFile.lines", dict(new_callable=PropertyMock)),
        ("CurrentVersionFile.section_name_re", dict(new_callable=PropertyMock)),
        prefix="tools.docs.rst_check")

    with patched as (m_enum, m_tokens, m_check, m_lines, m_section):
        m_enum.return_value = lines
        m_check.return_value = errors
        m_section.return_value.match.return_value = matches
        _result = version_file.run_checks()
        assert isinstance(_result, types.GeneratorType)
        result = list(_result)

    assert (
        list(m_enum.call_args)
        == [(m_lines.return_value,), {}])

    if not lines:
        assert result == []
        assert not m_section.return_value.match.called
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
        list(list(c) for c in m_section.return_value.match.call_args_list)
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
async def test_rst_checker_check_current_version(patches, errors):
    checker = rst_check.RSTChecker("path1", "path2", "path3")
    patched = patches(
        "pathlib",
        "CurrentVersionFile",
        "RSTChecker.error",
        prefix="tools.docs.rst_check")

    with patched as (m_plib, m_version, m_error):
        m_version.return_value.run_checks.return_value = errors
        assert not await checker.check_current_version()

    assert (
        list(m_plib.Path.call_args)
        == [('docs/root/version_history/current.rst',), {}])
    assert (
        list(m_version.call_args)
        == [(m_plib.Path.return_value,), {}])
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
