#!/usr/bin/env python3
"""Envoy Bazel query implementation.

This module can be used either as a `py_binary` or a `py_library`.
"""
import pathlib
import sys
from functools import cached_property

import abstracts

from envoy.base.utils import ABazelQuery

import envoy_repo


@abstracts.implementer(ABazelQuery)
class EnvoyBazelQuery:

    @cached_property
    def path(self) -> pathlib.Path:
        return pathlib.Path(envoy_repo.PATH)


query = EnvoyBazelQuery().query


def main(*args):
    print(query(*args[0:1]))


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))

__all__ = ("query",)
