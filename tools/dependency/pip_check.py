#!/usr/bin/env python3

import sys

from envoy.dependency import pip_check


def main(*args: str) -> int:
    return pip_check.main(*args)


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
