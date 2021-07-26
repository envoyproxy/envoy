import importlib
import sys
from contextlib import contextmanager

import pytest

from tools.base import utils


# this is necessary to fix coverage as these libs are imported before pytest
# is invoked
importlib.reload(utils)


def test_util_buffered_stdout():
    stdout = []

    with utils.buffered(stdout=stdout):
        print("test1")
        print("test2")
        sys.stdout.write("test3\n")
        sys.stderr.write("error0\n")

    assert stdout == ["test1", "test2", "test3"]


def test_util_buffered_stderr():
    stderr = []

    with utils.buffered(stderr=stderr):
        print("test1")
        print("test2")
        sys.stdout.write("test3\n")
        sys.stderr.write("error0\n")
        sys.stderr.write("error1\n")

    assert stderr == ["error0", "error1"]


def test_util_buffered_stdout_stderr():
    stdout = []
    stderr = []

    with utils.buffered(stdout=stdout, stderr=stderr):
        print("test1")
        print("test2")
        sys.stdout.write("test3\n")
        sys.stderr.write("error0\n")
        sys.stderr.write("error1\n")

    assert stdout == ["test1", "test2", "test3"]
    assert stderr == ["error0", "error1"]


def test_util_buffered_no_stdout_stderr():

    with pytest.raises(utils.BufferUtilError):
        with utils.buffered():
            pass


def test_util_nested():

    fun1_args = []
    fun2_args = []

    @contextmanager
    def fun1(arg):
        fun1_args.append(arg)
        yield "FUN1"

    @contextmanager
    def fun2(arg):
        fun2_args.append(arg)
        yield "FUN2"

    with utils.nested(fun1("A"), fun2("B")) as (fun1_yield, fun2_yield):
        assert fun1_yield == "FUN1"
        assert fun2_yield == "FUN2"

    assert fun1_args == ["A"]
    assert fun2_args == ["B"]


def test_util_coverage_with_data_file(patches):
    patched = patches(
        "ConfigParser",
        "tempfile.TemporaryDirectory",
        "os.path.join",
        "open",
        prefix="tools.base.utils")

    with patched as (m_config, m_tmp, m_join, m_open):
        with utils.coverage_with_data_file("PATH") as tmprc:
            assert tmprc == m_join.return_value
    assert (
        list(m_config.call_args)
        == [(), {}])
    assert (
        list(m_config.return_value.read.call_args)
        == [('.coveragerc',), {}])
    assert (
        list(m_config.return_value.__getitem__.call_args)
        == [('run',), {}])
    assert (
        list(m_config.return_value.__getitem__.return_value.__setitem__.call_args)
        == [('data_file', 'PATH'), {}])
    assert (
        list(m_tmp.call_args)
        == [(), {}])
    assert (
        list(m_join.call_args)
        == [(m_tmp.return_value.__enter__.return_value, '.coveragerc'), {}])
    assert (
        list(m_open.call_args)
        == [(m_join.return_value, 'w'), {}])
    assert (
        list(m_config.return_value.write.call_args)
        == [(m_open.return_value.__enter__.return_value,), {}])


def test_util_extract(patches):
    patched = patches(
        "tempfile.TemporaryDirectory",
        "tarfile.open",
        prefix="tools.base.utils")

    with patched as (m_tmp, m_open):
        assert utils.extract("TARBALL", "PATH") == "PATH"

    assert (
        list(m_open.call_args)
        == [('TARBALL',), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.extractall.call_args)
        == [(), {'path': "PATH"}])


def test_util_untar(patches):
    patched = patches(
        "tempfile.TemporaryDirectory",
        "extract",
        prefix="tools.base.utils")

    with patched as (m_tmp, m_extract):
        with utils.untar("PATH") as tmpdir:
            assert tmpdir == m_extract.return_value

    assert (
        list(m_tmp.call_args)
        == [(), {}])
    assert (
        list(m_extract.call_args)
        == [('PATH', m_tmp.return_value.__enter__.return_value), {}])


def test_util_from_yaml(patches):
    patched = patches(
        "open",
        "yaml",
        prefix="tools.base.utils")

    with patched as (m_open, m_yaml):
        assert utils.from_yaml("PATH") == m_yaml.safe_load.return_value

    assert (
        list(m_open.call_args)
        == [("PATH", ), {}])
    assert (
        list(m_yaml.safe_load.call_args)
        == [(m_open.return_value.__enter__.return_value.read.return_value, ), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.read.call_args)
        == [(), {}])


def test_util_to_yaml(patches):
    patched = patches(
        "open",
        "yaml",
        prefix="tools.base.utils")

    with patched as (m_open, m_yaml):
        assert utils.to_yaml("DATA", "PATH") == "PATH"

    assert (
        list(m_open.call_args)
        == [("PATH", "w"), {}])
    assert (
        list(m_yaml.dump.call_args)
        == [("DATA", ), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.write.call_args)
        == [(m_yaml.dump.return_value, ), {}])
