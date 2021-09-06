"""Transform bazel api labels into rst paths in the docs"""

from typing import Tuple


def format_proto_src(src: str) -> str:
    """Transform api bazel Label -> rst path in docs

    eg:
       @envoy_api//envoy/watchdog/v3alpha:abort_action.proto

       ->  envoy/watchdog/v3alpha/abort_action.proto.rst
    """
    return f"{src.replace(':', '/').strip('@').replace('//', '/')[10:]}.rst"


def main(data) -> Tuple[str]:
    return tuple(format_proto_src(src) for src in data if src)
