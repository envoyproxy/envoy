#!/usr/bin/env python3

import sys

from envoy.gpg import sign


def main(*args: str) -> int:
    return sign.main(*args)


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
