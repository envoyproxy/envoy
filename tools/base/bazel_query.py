#!/usr/bin/env python3
"""Envoy Bazel query implementation.

This module can be used either as a `py_binary` or a `py_library`.

cli usage (outputs to json):

```console
$ bazel run //tools/base:bazel_query "deps(source/...)" | jq "."
```

python usage:

```python
from tools.base.bazel_query import query

result = query("deps(source/...)")
```

NB: This allows running queries that do not define scope and cannot be
run as genqueries. **It should not therefore be used in build rules**.
"""

# The upstream lib is maintained here:
#
#    https://github.com/envoyproxy/pytooling/tree/main/envoy.base.utils
#
# Please submit issues/PRs to the pytooling repo:
#
#    https://github.com/envoyproxy/pytooling
#

import json
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
    print(json.dumps(query(*args[0:1])))


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))

__all__ = ("query",)
