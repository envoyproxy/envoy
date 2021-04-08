#
# Runs pytest against the target:
#
#   //tools/base:runner
#
# Can be run as follows:
#
#   bazel run //tools/base:pytest_runner
#

import sys

from tools.testing import python_pytest


def main(*args) -> int:
    return python_pytest.main(*args, "--cov", "tools.base")


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
