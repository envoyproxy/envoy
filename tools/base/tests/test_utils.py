import importlib

from tools.base import utils


# this is necessary to fix coverage
importlib.reload(utils)


def test_pytest_utils(check_pytest_target):
    check_pytest_target("tools.base.pytest_utils")


def test_util_custom_coverage_data(patches):
    patched = patches(
        "ConfigParser",
        "tempfile.TemporaryDirectory",
        "os.path.join",
        "open",
        prefix="tools.base.utils")

    with patched as (m_config, m_tmp, m_join, m_open):
        with utils.custom_coverage_data("PATH") as tmprc:
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
