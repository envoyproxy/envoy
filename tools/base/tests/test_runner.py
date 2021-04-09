import importlib
from unittest.mock import patch, PropertyMock

from tools.base import runner


# this is necessary to fix coverage as these libs are imported before pytest
# is invoked
importlib.reload(runner)


def test_runner_constructor():
    run = runner.Runner("path1", "path2", "path3")
    assert run._args == ("path1", "path2", "path3")


def test_runner_args():
    run = runner.Runner("path1", "path2", "path3")
    parser_mock = patch(
        "tools.base.runner.Runner.parser",
        new_callable=PropertyMock)

    with parser_mock as m_parser:
        assert run.args == m_parser.return_value.parse_known_args.return_value.__getitem__.return_value

    assert (
        list(m_parser.return_value.parse_known_args.call_args)
        == [(('path1', 'path2', 'path3'),), {}])
    assert (
        list(m_parser.return_value.parse_known_args.return_value.__getitem__.call_args)
        == [(0,), {}])
    assert "args" in run.__dict__


def test_runner_extra_args():
    run = runner.Runner("path1", "path2", "path3")
    parser_mock = patch(
        "tools.base.runner.Runner.parser",
        new_callable=PropertyMock)

    with parser_mock as m_parser:
        assert run.extra_args == m_parser.return_value.parse_known_args.return_value.__getitem__.return_value

    assert (
        list(m_parser.return_value.parse_known_args.call_args)
        == [(('path1', 'path2', 'path3'),), {}])
    assert (
        list(m_parser.return_value.parse_known_args.return_value.__getitem__.call_args)
        == [(1,), {}])
    assert "extra_args" in run.__dict__


def test_runner_parser(patches):
    run = runner.Runner("path1", "path2", "path3")
    patched = patches(
        "argparse.ArgumentParser",
        "Runner.add_arguments",
        prefix="tools.base.runner")
    with patched as (m_parser, m_add_args):
        assert run.parser == m_parser.return_value

    assert (
        list(m_parser.call_args)
        == [(), {"allow_abbrev": False}])
    assert (
        list(m_add_args.call_args)
        == [(m_parser.return_value,), {}])
    assert "parser" in run.__dict__


def test_runner_add_arguments(patches):
    run = runner.Runner("path1", "path2", "path3")
    assert run.add_arguments("PARSER") is None
