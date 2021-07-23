from itertools import chain
from unittest.mock import AsyncMock, MagicMock, PropertyMock

import pytest

from tools.base.checker import Checker
from tools.distribution import verify


def test_checker_constructor(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    assert isinstance(checker, Checker)
    assert checker._active_test is None
    assert checker.checks == ("distros", )
    assert checker._test_types == ()


def test_checker_cls_register_test():
    assert verify.PackagesDistroChecker._test_types == ()

    class Test1(object):
        pass

    class Test2(object):
        pass

    verify.PackagesDistroChecker.register_test("test1", Test1)
    assert (
        verify.PackagesDistroChecker._test_types
        == (('test1', Test1),))

    verify.PackagesDistroChecker.register_test("test2", Test2)
    assert (
        verify.PackagesDistroChecker._test_types
        == (('test1', Test1),
            ('test2', Test2),))


def _check_arg_property(patches, prop, arg=None, cached=False):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    assert isinstance(checker, Checker)

    patched = patches(
        ("PackagesDistroChecker.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_args, ):
        assert getattr(checker, prop) == getattr(m_args.return_value, arg or prop)

    if cached:
        assert prop in checker.__dict__
    else:
        assert prop not in checker.__dict__


@pytest.mark.parametrize(
    "prop",
    [("testfile",),
     ("test_distributions", "distribution"),
     ("packages_tarball", "packages")])
def test_checker_arg_props(patches, prop):
    _check_arg_property(patches, *prop)


def test_checker_active_test(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    assert checker.active_test is None
    checker._active_test = "ATEST"
    assert checker.active_test == "ATEST"
    assert "active_test" not in checker.__dict__


def test_checker_config(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "open",
        "yaml",
        ("PackagesDistroChecker.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_open, m_yaml, m_args):
        assert checker.config == m_yaml.safe_load.return_value

    assert (
        list(m_open.call_args)
        == [(m_args.return_value.config,), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.read.call_args)
        == [(), {}])
    assert (
        list(m_yaml.safe_load.call_args)
        == [(m_open.return_value.__enter__.return_value.read.return_value,), {}])
    assert "config" in checker.__dict__


def test_checker_distro_test_class(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "distrotest",
        prefix="tools.distribution.verify")

    with patched as (m_test, ):
        assert checker.distro_test_class == m_test.DistroTest

    assert "distro_test_class" not in checker.__dict__


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


def test_checker_packages_dir(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "os",
        "tarfile",
        ("PackagesDistroChecker.packages_tarball", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.path", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_os, m_tar, m_tarb, m_path):
        assert checker.packages_dir == m_os.path.join.return_value

    assert (
        list(m_os.path.join.call_args)
        == [(m_path.return_value, 'packages'), {}])
    assert (
        list(m_tar.open.call_args)
        == [(m_tarb.return_value,), {}])
    assert (
        list(m_tar.open.return_value.__enter__.return_value.extractall.call_args)
        == [(), {'path': m_os.path.join.return_value}])


def test_checker_path(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.tempdir", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_temp, ):
        assert checker.path == m_temp.return_value.name

    assert "path" not in checker.__dict__


def test_checker_tempdir(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "tempfile",
        prefix="tools.distribution.verify")

    with patched as (m_temp, ):
        assert checker.tempdir == m_temp.TemporaryDirectory.return_value

    assert (
        list(m_temp.TemporaryDirectory.call_args)
        == [(), {}])
    assert "tempdir" in checker.__dict__


@pytest.mark.parametrize(
    "config",
    [{},
     {f"DISTRO{i}":dict(foo="FOO", bar="BAR") for i in range(1, 4)},
     {f"DISTRO{i}":dict(test_type="OTHER", foo="FOO", bar="BAR") for i in range(1, 4)}])
@pytest.mark.parametrize(
    "distributions",
    [None,
     [],
     ["DISTRO1", "DISTRO2", "DISTRO3"],
     ["DISTRO1", "DISTRO3"]])
def test_checker_tests(patches, config, distributions):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "PackagesDistroChecker._test_type",
        "PackagesDistroChecker._test_packages",
        ("PackagesDistroChecker.config", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.test_distributions", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_type, m_pkgs, m_config, m_tests):
        m_config.return_value = config.copy()
        m_tests.return_value = distributions
        result = checker.tests

    if distributions:
        config = {k: v for k, v in config.items() if k in distributions}

    assert len(config) == len(m_type.call_args_list)
    assert len(config) == len(m_pkgs.call_args_list)

    for k, v in config.items():
        v["test_type"] = m_type.return_value
        v["packages"] = m_pkgs.return_value

    assert result == config
    assert (
        list(list(c) for c in m_type.call_args_list)
        == [[(distro,), {}] for distro in config])
    assert (
        list(list(c) for c in m_pkgs.call_args_list)
        == [[(m_type.return_value,), {}] for i in config])


def test_checker_test_types(patches):
    checker = verify.PackagesDistroChecker("x", "y", "z")
    _utils = (("NAME1", "UTIL1"), ("NAME2", "UTIL2"))
    checker._test_types = _utils
    assert checker.test_types == dict(_utils)


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
            [('--distribution', '-d'),
             {'nargs': '?',
              'help': 'Specify distribution to test. Can be specified multiple times.'}]])


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "tests",
    [{},
     {f"DISTRO{i}":dict(packages=[f"P{i}A", f"P{i}B", f"P{i}C"]) for i in range(1, 4)}])
async def test_checker_check_distros(patches, tests):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "PackagesDistroChecker.distro_test",
        ("PackagesDistroChecker.log", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.tests", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_dtest, m_log, m_tests):
        m_tests.return_value.items.return_value = tests.items()
        assert not await checker.check_distros()

    assert (
        list(list(c) for c in m_log.return_value.info.call_args_list)
        == [[(f'[{distro}] Testing with: {",".join(tests[distro]["packages"])}',), {}]
            for distro in tests])
    expected = list(
        chain.from_iterable(
            [[(distro, package, tests[distro]), {}]
             for package in tests[distro]["packages"]]
            for distro in tests))
    assert (
        list(list(c) for c in m_dtest.call_args_list)
        == expected)


@pytest.mark.asyncio
@pytest.mark.parametrize("exiting", [True, False])
@pytest.mark.parametrize("errors", [None, (), ("ERR1", "ERR")])
async def test_checker_distro_test(patches, exiting, errors):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "PackagesDistroChecker.error",
        ("PackagesDistroChecker.exiting", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.log", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.path", dict(new_callable=PropertyMock)),
        ("PackagesDistroChecker.test_types", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")
    config = dict(
        test_type="TESTTYPE",
        image="IMAGE",
        tag="TAG")

    with patched as (m_error, m_exit, m_log, m_path, m_types):
        m_exit.return_value = exiting
        m_types.return_value.__getitem__.return_value.return_value.run = AsyncMock(
            return_value=errors)
        assert not await checker.distro_test("DISTRO", "PACKAGE", config)

    if exiting:
        assert not m_log.called
        assert not m_types.called
        assert not m_error.called
        assert not checker._active_test
        return

    assert (
        checker._active_test
        == m_types.return_value.__getitem__.return_value.return_value)
    assert (
        list(m_log.return_value.info.call_args)
        == [('[DISTRO] Testing package: PACKAGE',), {}])
    assert (
        list(m_types.return_value.__getitem__.return_value.return_value.run.call_args)
        == [(), {}])
    assert (
        list(m_types.return_value.__getitem__.return_value.call_args)
        == [(checker, m_path.return_value, 'PACKAGE', 'DISTRO', 'IMAGE', 'TAG'), {}])

    if errors:
        assert (
            list(m_error.call_args)
            == [('distros', ('ERR1', 'ERR')), {}])
    else:
        assert not m_error.called


@pytest.mark.asyncio
async def test_checker_on_checks_complete(patches):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "PackagesDistroChecker._cleanup_test",
        "PackagesDistroChecker._cleanup_docker",
        "PackagesDistroChecker._cleanup_tempdir",
        "checker.Checker.on_checks_complete",
        prefix="tools.distribution.verify")
    order_mock = MagicMock()

    with patched as (m_test, m_docker, m_temp, m_complete):
        m_test.side_effect = lambda: order_mock("TEST")
        m_docker.side_effect = lambda: order_mock("DOCKER")
        m_temp.side_effect = lambda: order_mock("TEMP")
        m_complete.side_effect = lambda: (order_mock('COMPLETE') and "COMPLETE")
        assert await checker.on_checks_complete() == "COMPLETE"

    assert (
        (list(list(c) for c in order_mock.call_args_list))
        == [[('TEST',), {}],
            [('DOCKER',), {}],
            [('TEMP',), {}],
            [('COMPLETE',), {}]])

    for m in m_test, m_docker, m_temp, m_complete:
        assert (
            list(m.call_args)
            == [(), {}])


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
async def test_checker__cleanup_tempdir(patches, exists):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.tempdir", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    if exists:
        checker.__dict__["tempdir"] = "TEMPDIR"

    with patched as (m_tempdir, ):
        await checker._cleanup_tempdir()

    assert "tempdir" not in checker.__dict__

    if not exists:
        assert not m_tempdir.return_value.cleanup.called
        return

    assert (
        list(m_tempdir.return_value.cleanup.call_args)
        == [(), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("exists", [True, False])
async def test_checker__cleanup_test(patches, exists):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        ("PackagesDistroChecker.active_test", dict(new_callable=PropertyMock)),
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


@pytest.mark.parametrize("pkg", [None, "PKG"])
def test_checker__pkg_path(patches, pkg):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "os",
        ("PackagesDistroChecker.packages_dir", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_os, m_dir):
        assert checker._pkg_path("PKG_TYPE", pkg) == m_os.path.join.return_value

    expected = [m_dir.return_value, "PKG_TYPE"]
    if pkg:
        expected.append(pkg)

    assert (
        list(m_os.path.join.call_args)
        == [tuple(expected), {}])


@pytest.mark.parametrize("pkg_type", ["TYPE1", "TYPE2"])
@pytest.mark.parametrize(
    "packages",
    [[],
     ["foo.TYPE1"],
     ["foo.TYPE1", "bar.NOTTYPE1", "foo.TYPE3", "bar.TYPE3"],
     ["foo.TYPE1", "bar.TYPE1", "foo.TYPE3", "bar.TYPE3"]])
def test_checker__test_packages(patches, pkg_type, packages):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    patched = patches(
        "os",
        "PackagesDistroChecker._pkg_path",
        prefix="tools.distribution.verify")
    matching = [x for x in packages if x.endswith(f".{pkg_type}")]

    with patched as (m_os, m_path):
        m_os.listdir.return_value = packages
        result = checker._test_packages(pkg_type)

    assert result == [m_path.return_value for x in matching]
    assert (
        list(m_os.listdir.call_args)
        == [(m_path.return_value,), {}])
    assert (
        list(list(c) for c in m_path.call_args_list)
        == ([[(pkg_type,), {}]]
            + [[(pkg_type, match), {}] for match in matching]))


@pytest.mark.parametrize(
    "distro",
    ["",
     "debianFoo", "debian", "debian.foo", "debian_foo", "notdebian",
     "ubuntuFoo", "ubuntu", "ubuntu.foo", "ubuntu_foo", "notubuntu",
     "otherFoo", "other", "other.foo", "other_foo", "notother"])
def test_checker__test_type(patches, distro):
    checker = verify.PackagesDistroChecker("path1", "path2", "path3")
    result = checker._test_type(distro)
    if distro.startswith("debian"):
        assert result == "deb"
    elif distro.startswith("ubuntu"):
        assert result == "deb"
    else:
        assert result == "rpm"


# Module

def test_verify_main(patches, command_main):
    patched = patches(
        "_register_tests",
        prefix="tools.distribution.verify")

    with patched as (m_reg, ):
        command_main(
            verify.main,
            "tools.distribution.verify.PackagesDistroChecker")

    assert (
        list(m_reg.call_args)
        == [(), {}])


def test_verify_register_tests(patches, command_main):
    patched = patches(
        "distrotest",
        "PackagesDistroChecker.register_test",
        prefix="tools.distribution.verify")

    with patched as (m_test, m_reg):
        verify._register_tests()

    assert (
        list(list(c) for c in m_reg.call_args_list)
        == [[('deb', m_test.DebDistroTest), {}],
            [('rpm', m_test.RPMDistroTest), {}]])
