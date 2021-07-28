from unittest.mock import patch, PropertyMock

import pytest

from tools.dependency import pip_check


def test_pip_checker_constructor():
    checker = pip_check.PipChecker("path1", "path2", "path3")
    assert checker.checks == ("dependabot",)
    assert checker.dependabot_config_path == pip_check.DEPENDABOT_CONFIG == ".github/dependabot.yml"
    assert checker.requirements_filename == pip_check.REQUIREMENTS_FILENAME == "requirements.txt"
    assert checker.args.paths == ['path1', 'path2', 'path3']


def test_pip_checker_config_requirements():
    checker = pip_check.PipChecker("path1", "path2", "path3")

    config_mock = patch(
        "tools.dependency.pip_check.PipChecker.dependabot_config",
        new_callable=PropertyMock)

    with config_mock as m_config:
        m_config.return_value.__getitem__.return_value = [
            {"package-ecosystem": "pip", "directory": "dir1"},
            {"package-ecosystem": "not-pip", "directory": "dir2"},
            {"package-ecosystem": "pip", "directory": "dir3"}]
        assert checker.config_requirements == {'dir1', 'dir3'}
        assert (
            list(m_config.return_value.__getitem__.call_args)
            == [('updates',), {}])


def test_pip_checker_dependabot_config(patches):
    checker = pip_check.PipChecker("path1", "path2", "path3")
    patched = patches(
        "utils",
        ("PipChecker.path", dict(new_callable=PropertyMock)),
        "os.path.join",
        prefix="tools.dependency.pip_check")

    with patched as (m_utils, m_path, m_join):
        assert checker.dependabot_config == m_utils.from_yaml.return_value

    assert (
        list(m_join.call_args)
        == [(m_path.return_value, checker._dependabot_config), {}])
    assert (
        list(m_utils.from_yaml.call_args)
        == [(m_join.return_value,), {}])


def test_pip_checker_requirements_dirs(patches):
    checker = pip_check.PipChecker("path1", "path2", "path3")

    dummy_walker = [
        ["ROOT1", ["DIR1", "DIR2"], ["FILE1", "FILE2", "FILE3"]],
        ["ROOT2", ["DIR1", "DIR2"], ["FILE1", "FILE2", "REQUIREMENTS_FILE", "FILE3"]],
        ["ROOT3", ["DIR1", "DIR2"], ["FILE1", "FILE2", "REQUIREMENTS_FILE", "FILE3"]],
        ["ROOT4", ["DIR1", "DIR2"], ["FILE1", "FILE2", "FILE3"]]]

    patched = patches(
        ("PipChecker.requirements_filename", dict(new_callable=PropertyMock)),
        ("PipChecker.path", dict(new_callable=PropertyMock)),
        "os.walk",
        prefix="tools.dependency.pip_check")

    with patched as (m_reqs, m_path, m_walk):
        m_reqs.return_value = "REQUIREMENTS_FILE"
        m_path.return_value = "ROO"
        m_walk.return_value = dummy_walker
        assert checker.requirements_dirs == {'T3', 'T2'}

    assert m_reqs.called
    assert m_path.called
    assert (
        list(m_walk.call_args)
        == [('ROO',), {}])


TEST_REQS = (
    (set(), set()),
    (set(["A", "B"]), set()),
    (set(["A", "B"]), set(["B", "C"])),
    (set(["A", "B", "C"]), set(["A", "B", "C"])),
    (set(), set(["B", "C"])))


@pytest.mark.parametrize("requirements", TEST_REQS)
def test_pip_checker_check_dependabot(patches, requirements):
    config, dirs = requirements
    checker = pip_check.PipChecker("path1", "path2", "path3")

    patched = patches(
        ("PipChecker.config_requirements", dict(new_callable=PropertyMock)),
        ("PipChecker.requirements_dirs", dict(new_callable=PropertyMock)),
        ("PipChecker.requirements_filename", dict(new_callable=PropertyMock)),
        "PipChecker.dependabot_success",
        "PipChecker.dependabot_errors",
        prefix="tools.dependency.pip_check")

    with patched as (m_config, m_dirs, m_fname, m_success, m_errors):
        m_config.return_value = config
        m_dirs.return_value = dirs
        assert not checker.check_dependabot()

    if config & dirs:
        assert (
            list(m_success.call_args)
            == [(config & dirs, ), {}])
    else:
        assert not m_success.called

    if config - dirs:
        assert (
            [(config - dirs, f"Missing {m_fname.return_value} dir, specified in dependabot config"), {}]
            in list(list(c) for c in m_errors.call_args_list))

    if dirs - config:
        assert (
            [(dirs - config, f"Missing dependabot config for {m_fname.return_value} in dir"), {}]
            in list(list(c) for c in m_errors.call_args_list))

    if not config - dirs and not dirs - config:
        assert not m_errors.called


def test_pip_checker_dependabot_success(patches):
    checker = pip_check.PipChecker("path1", "path2", "path3")
    succeed_mock = patch
    success = set(["C", "D", "B", "A"])

    patched = patches(
        "PipChecker.succeed",
        ("PipChecker.requirements_filename", dict(new_callable=PropertyMock)),
        prefix="tools.dependency.pip_check")

    with patched as (m_succeed, m_fname):
        checker.dependabot_success(success)

    assert (
        list(m_succeed.call_args)
        == [('dependabot',
             [f"{m_fname.return_value}: {x}" for x in sorted(success)]),  {}])


def test_pip_checker_dependabot_errors(patches):
    checker = pip_check.PipChecker("path1", "path2", "path3")
    succeed_mock = patch
    errors = set(["C", "D", "B", "A"])
    MSG = "ERROR MESSAGE"

    patched = patches(
        "PipChecker.error",
        ("PipChecker.name", dict(new_callable=PropertyMock)),
        prefix="tools.dependency.pip_check")

    with patched as (m_error, m_name):
        checker.dependabot_errors(errors, MSG)

    assert (
        list(list(c) for c in list(m_error.call_args_list))
        == [[('dependabot', [f'ERROR MESSAGE: {x}']), {}] for x in sorted(errors)])


def test_pip_checker_main():
    class_mock = patch("tools.dependency.pip_check.PipChecker")

    with class_mock as m_class:
        assert (
            pip_check.main("arg0", "arg1", "arg2")
            == m_class.return_value.run.return_value)

    assert (
        list(m_class.call_args)
        == [('arg0', 'arg1', 'arg2'), {}])
    assert (
        list(m_class.return_value.run.call_args)
        == [(), {}])
