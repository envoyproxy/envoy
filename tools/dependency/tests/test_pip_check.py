from unittest.mock import MagicMock, patch, PropertyMock

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


@pytest.mark.parametrize("isdict", [True, False])
def test_pip_checker_dependabot_config(patches, isdict):
    checker = pip_check.PipChecker("path1", "path2", "path3")
    patched = patches(
        "utils",
        ("PipChecker.path", dict(new_callable=PropertyMock)),
        prefix="tools.dependency.pip_check")

    with patched as (m_utils, m_path):
        if isdict:
            m_utils.from_yaml.return_value = {}

        if isdict:
            assert checker.dependabot_config == m_utils.from_yaml.return_value
        else:
            with pytest.raises(pip_check.PipConfigurationError) as e:
                checker.dependabot_config

            assert (
                e.value.args[0]
                == f'Unable to parse dependabot config: {checker.dependabot_config_path}')

    assert (
        list(m_path.return_value.joinpath.call_args)
        == [(checker._dependabot_config, ), {}])
    assert (
        list(m_utils.from_yaml.call_args)
        == [(m_path.return_value.joinpath.return_value,), {}])


def test_pip_checker_requirements_dirs(patches):
    checker = pip_check.PipChecker("path1", "path2", "path3")
    dummy_glob = [
        "FILE1", "FILE2", "FILE3",
        "REQUIREMENTS_FILE", "FILE4",
        "REQUIREMENTS_FILE", "FILE5"]
    patched = patches(
        ("PipChecker.requirements_filename", dict(new_callable=PropertyMock)),
        ("PipChecker.path", dict(new_callable=PropertyMock)),
        prefix="tools.dependency.pip_check")
    expected = []

    with patched as (m_reqs, m_path):
        m_reqs.return_value = "REQUIREMENTS_FILE"
        _glob = []

        for fname in dummy_glob:
            _mock = MagicMock()
            _mock.name = fname
            if fname == "REQUIREMENTS_FILE":
                expected.append(_mock)
            _glob.append(_mock)

        m_path.return_value.glob.return_value = _glob
        assert checker.requirements_dirs == {f"/{f.parent.relative_to.return_value}" for f in expected}

    for exp in expected:
        assert (
            list(exp.parent.relative_to.call_args)
            == [(m_path.return_value,), {}])
    assert "requirements_dirs" in checker.__dict__


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
