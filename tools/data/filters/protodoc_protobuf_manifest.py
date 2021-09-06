
import json
from typing import Dict

from google.protobuf import json_format

from tools.protodoc import manifest_pb2


def main(data: Dict) -> manifest_pb2.Manifest:
    # Load as YAML, emit as JSON and then parse as proto to provide type
    # checking.
    # As this returns non-JSON-serializable data it cannot be cached.
    out = manifest_pb2.Manifest()
    json_format.Parse(json.dumps(data), out)
    return out
