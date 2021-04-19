
from unittest.mock import patch

from tools.code_format import rst_check


def test_python_checker_main():
    class_mock = patch("tools.code_format.rst_check.RstChecker")

    with class_mock as m_class:
        assert (
            rst_check.main("arg0", "arg1", "arg2")
            == m_class.return_value.run_checks.return_value)

    assert (
        list(m_class.call_args)
        == [('arg0', 'arg1', 'arg2'), {}])
    assert (
        list(m_class.return_value.run_checks.call_args)
        == [(), {}])
