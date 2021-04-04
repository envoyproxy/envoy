import sys

from tools.testing import python_pytest


def main(*args):
    return python_pytest.main(*args, "--cov", "tools.code_format")


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
