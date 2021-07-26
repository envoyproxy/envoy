from itertools import chain
from unittest.mock import AsyncMock, MagicMock, PropertyMock

import pytest

from tools.base.checker import AsyncChecker
from tools.distribution import distrotest, verify


def test_checker_constructor(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    assert isinstance(checker, AsyncChecker)
    assert checker._active_distrotest is None
    assert checker.checks == ("distros", )

    assert checker.test_class == distrotest.DistroTest
    assert "test_class" not in checker.__dict__
    assert checker.test_config_class == distrotest.DistroTestConfig
    assert "test_config_class" not in checker.__dict__


def _check_arg_property(patches, prop, arg=None):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")

    patched = patches(
        ("PackagesDistroChecker.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_args, ):
        assert getattr(checker, prop) == getattr(m_args.return_value, arg or prop)

    assert prop not in checker.__dict__


@pytest.mark.parametrize(
    "prop",
    [("rebuild",),
     ("filter_distributions", "distribution")])
def test_checker_arg_props(patches, prop):
    _check_arg_property(patches, *prop)


def _check_arg_path_property(patches, prop, arg=None):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "pathlib",
        ("PackagesDistroChecker.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_plib, m_args):
        assert getattr(checker, prop) == m_plib.Path.return_value

    assert (
        list(m_plib.Path.call_args)
        == [(getattr(m_args.return_value, arg or prop), ), {}])
    assert prop not in checker.__dict__


@pytest.mark.parametrize(
    "prop",
    [("testfile",),
     ("keyfile",),
     ("packages_tarball", "packages")])
def test_checker_arg_path_props(patches, prop):
    _check_arg_path_property(patches, *prop)


def test_checker_active_distrotest(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    assert checker.active_distrotest is None
    checker._active_distrotest = "ATEST"
    assert checker.active_distrotest == "ATEST"
    assert "active_distrotest" not in checker.__dict__


@pytest.mark.parametrize("is_dict", [True, False])
def test_checker_config(patches, is_dict):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "isinstance",
        "utils",
        ("PackagesDistroChecker.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_inst, m_utils, m_args):
        m_inst.return_value = is_dict
        if is_dict:
            assert checker.config == m_utils.from_yaml.return_value
        else:

            with pytest.raises(verify.PackagesConfigurationError) as e:
                checker.config

    assert (
        list(m_utils.from_yaml.call_args)
        == [(m_args.return_value.config,), {}])

    if is_dict:
        assert "config" in checker.__dict__
    else:
        assert (
            e.value.args[0]
            == f"Unable to parse configuration {m_args.return_value.config}")


def test_checker_docker(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "aiodocker",
        prefix="tools.distribution.verify")

    with patched as (m_docker, ):
        assert checker.docker == m_docker.Docker.return_value

    assert (
        list(m_docker.Docker.call_args)
        == [(), {}])
    assert "docker" in checker.__dict__


def test_checker_path(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "pathlib",
        ("PackagesDistroChecker.tempdir", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_plib, m_temp):
        assert checker.path == m_plib.Path.return_value

    assert (
        list(m_plib.Path.call_args)
        == [(m_temp.return_value.name, ), {}])
    assert "path" not in checker.__dict__


def test_checker_test_config(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.docker", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.keyfile", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.packages_tarball", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.path", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.test_config_class", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.testfile", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_docker, m_key, m_tar, m_path, m_class, m_test):
        assert checker.test_config == m_class.return_value.return_value

    assert (
        list(m_class.return_value.call_args)
        == [(),
            {'docker': m_docker.return_value,
             'keyfile': m_key.return_value,
             'path': m_path.return_value,
             'tarball': m_tar.return_value,
             'testfile': m_test.return_value,
             'maintainer': verify.ENVOY_MAINTAINER,
             'version': verify.ENVOY_VERSION}])
    assert "test_config" in checker.__dict__


@pytest.mark.parametrize(
    "config",
    [{},
     {f"DISTRO{i}": dict(image="SOMEIMAGE", ext="EXT1", foo="FOO", bar="BAR") for i in range(1, 4)},
     {f"DISTRO{i}": dict(image="OTHERIMAGE", ext="EXT2", foo="FOO", bar="BAR") for i in range(1, 4)}])
@pytest.mark.parametrize(
    "distributions",
    [None,
     [],
     ["DISTRO1", "DISTRO2", "DISTRO3"],
     ["DISTRO1", "DISTRO3"]])
def test_checker_tests(patches, config, distributions):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "PackagesDistroChecker.get_test_config",
        "PackagesDistroChecker.get_test_packages",
        ("PackagesDistroChecker.config", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.filter_distributions", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_tconfig, m_pkgs, m_config, m_tests):
        m_config.return_value = config.copy()
        m_tests.return_value = distributions
        result = checker.tests

    if distributions:
        config = {k: v for k, v in config.items() if k in distributions}

    assert (
        len(result)
        == len(config)
        == len(m_pkgs.call_args_list)
        == len(m_tconfig.call_args_list))

    for i, k in enumerate(result):
        assert k == (list(config)[i])
        assert result[k] == m_tconfig.return_value

    assert (
        list(list(c) for c in m_tconfig.call_args_list)
        == [[(_config["image"], ), {}] for _config in config.values()])
    assert (
        list(list(c) for c in m_tconfig.return_value.update.call_args_list)
        == [[(_conf,), {}] for _conf in config.values()])
    assert (
        list(list(c) for c in m_tconfig.return_value.__getitem__.call_args_list)
        == [[('type',), {}], [('ext',), {}]] * len(config))
    assert (
        list(list(c) for c in m_pkgs.call_args_list)
        == [[(m_tconfig.return_value.__getitem__.return_value, ) * 2, {}]] * len(config))


def test_checker_add_arguments():
    checker = verify.PackagesDistroChecker("x", "y", "z")
    parser = MagicMock()
    checker.add_arguments(parser)
    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('--log-level', '-l'),
             {'choices': ['debug', 'info', 'warn', 'error'],
              'default': 'info',
              'help': 'Log level to display'}],
            [('--fix',),
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
             {'choices': ('distros',),
              'nargs': '*',
              'help': 'Specify which checks to run, can be specified for multiple checks'}],
            [('--config-distros',),
             {'default': '',
              'help': 'Custom configuration for the distros check'}],
            [('--path', '-p'),
             {'default': None,
              'help': 'Path to the test root (usually Envoy source dir). If not specified the first path of paths is used'}],
            [('paths',),
             {'nargs': '*',
              'help': 'Paths to check. At least one path must be specified, or the `path` argument should be provided'}],
            [('testfile',),
             {'help': 'Path to the test file that will be run inside the distribution containers'}],
            [('config',),
             {'help': 'Path to a YAML configuration with distributions for testing'}],
            [('packages',),
             {'help': 'Path to a tarball containing packages to test'}],
            [('--keyfile', '-k'),
             {'help': 'Specify the path to a file containing a gpg key for verifying packages.'}],
            [('--distribution', '-d'),
             {'nargs': '?',
              'help': 'Specify distribution to test. Can be specified multiple times.'}],
            [('--rebuild',),
             {'action': 'store_true',
              'help': 'Rebuild test images before running the tests.'}]])


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "tests",
    [{},
     {f"DISTRO{i}": dict(image=f"IMAGE{i}")
      for i in range(1, 4)}])
@pytest.mark.parametrize("rebuild", [True, False])
async def test_checker_check_distros(patches, tests, rebuild):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "PackagesDistroChecker.run_test",
        ("PackagesDistroChecker.log", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.rebuild", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.tests", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    _items = {}
    for i, (k, v) in enumerate(tests.items()):
        v["packages"] = []
        for x in range(0, 3):
            _mock = MagicMock()
            _mock.name = f"P{i}{x}"
            v["packages"].append(_mock)
        _items[k] = v

    with patched as (m_dtest, m_log, m_rebuild, m_tests):
        m_tests.return_value.items.return_value = _items.items()
        m_rebuild.return_value = rebuild
        assert not await checker.check_distros()

    assert (
        list(list(c) for c in m_log.return_value.info.call_args_list)
        == [[(f'[{name}] Testing with: {",".join(n.name for n in tests[name]["packages"])}',), {}]
            for name in tests])
    expected = list(
        chain.from_iterable(
            [[(name, tests[name]["image"], package, (i == 0 and rebuild)), {}]
             for i, package in enumerate(tests[name]["packages"])]
            for name in tests))
    assert (
        list(list(c) for c in m_dtest.call_args_list)
        == expected)


def test_checker_get_test_config(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.test_config", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_config, ):
        assert checker.get_test_config("IMAGE") == m_config.return_value.get_config.return_value

    assert (
        list(m_config.return_value.get_config.call_args)
        == [('IMAGE',), {}])


def test_checker_get_test_packages(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.test_config", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_config, ):
        assert checker.get_test_packages("TYPE", "EXT") == m_config.return_value.get_packages.return_value

    assert (
        list(m_config.return_value.get_packages.call_args)
        == [('TYPE', 'EXT'), {}])


@pytest.mark.asyncio
async def test_checker_on_checks_complete(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "PackagesDistroChecker._cleanup_test",
        "PackagesDistroChecker._cleanup_docker",
        "checker.BaseChecker.on_checks_complete",
        prefix="tools.distribution.verify")
    order_mock = MagicMock()

    with patched as (m_test, m_docker, m_complete):
        m_test.side_effect = lambda: order_mock("TEST")
        m_docker.side_effect = lambda: order_mock("DOCKER")
        m_complete.side_effect = lambda: (order_mock('COMPLETE') and "COMPLETE")
        assert await checker.on_checks_complete() == "COMPLETE"

    assert (
        (list(list(c) for c in order_mock.call_args_list))
        == [[('TEST',), {}],
            [('DOCKER',), {}],
            [('COMPLETE',), {}]])

    for m in m_test, m_docker, m_complete:
        assert (
            list(m.call_args)
            == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("exiting", [True, False])
@pytest.mark.parametrize("errors", [None, (), ("ERR1", "ERR")])
@pytest.mark.parametrize("rebuild", [True, False])
async def test_checker_run_test(patches, exiting, errors, rebuild):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.test_class", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.test_config", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.exiting", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.log", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")
    config = dict(
        type="TESTTYPE",
        image="IMAGE")

    with patched as (m_test, m_config, m_exit, m_log):
        m_exit.return_value = exiting
        m_test.return_value.return_value.run = AsyncMock(
            return_value=errors)
        assert not await checker.run_test("NAME", "IMAGE", "PACKAGE", rebuild)

    if exiting:
        assert not m_log.called
        assert not m_test.called
        assert not checker._active_distrotest
        return

    assert (
        checker._active_distrotest
        == m_test.return_value.return_value)
    assert (
        list(m_log.return_value.info.call_args)
        == [('[NAME] Testing package: PACKAGE',), {}])
    assert (
        list(m_test.return_value.call_args)
        == [(checker, m_config.return_value, 'NAME', 'IMAGE', 'PACKAGE'), {"rebuild": rebuild}])


@pytest.mark.asyncio
@pytest.mark.parametrize("exists", [True, False])
async def test_checker__cleanup_docker(patches, exists):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.docker", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    if exists:
        checker.__dict__["docker"] = "DOCKER"

    with patched as (m_docker, ):
        m_docker.return_value.close = AsyncMock()
        await checker._cleanup_docker()

    assert "docker" not in checker.__dict__

    if not exists:
        assert not m_docker.return_value.close.called
        return

    assert (
        list(m_docker.return_value.close.call_args)
        == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("exists", [True, False])
async def test_checker__cleanup_test(patches, exists):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.active_distrotest", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_active, ):
        if not exists:
            m_active.return_value = None
        else:
            m_active.return_value.cleanup = AsyncMock()
        await checker._cleanup_test()

    if not exists:
        return

    assert (
        list(m_active.return_value.cleanup.call_args)
        == [(), {}])


# Module

def test_verify_main(patches, command_main):
    command_main(
        verify.main,
        "tools.distribution.verify.PackagesDistroChecker")
