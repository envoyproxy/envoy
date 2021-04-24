import sys

from tools.testing import python_pytest


def main(*args) -> int:
    return python_pytest.main(*args, "--cov", "_PACKAGE_NAME_")


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
