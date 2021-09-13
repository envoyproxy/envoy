#!/usr/bin/env python3

import sys

from __UPSTREAM_PACKAGE__ import main as upstream_main


def main(*args: str) -> int:
    return upstream_main(*args)


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
