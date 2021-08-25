#!/usr/bin/env python3

import sys

from envoy.distribution import verify


def main(*args: str) -> int:
    return verify.main(*args)


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
