import json
import pathlib
import sys

from google.protobuf import json_format

sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]

from tools.config_validation import validate_fragment
from tools.protodoc import manifest_pb2
from tools.protodoc.protodoc_manifest_untyped import data as protodoc_manifest_untyped


def main(output):
    # Load as YAML, emit as JSON and then parse as proto to provide type
    # checking.
    manifest = json_format.Parse(json.dumps(protodoc_manifest_untyped), manifest_pb2.Manifest())
    result = {}
    for field_name in manifest.fields:
        field = manifest.fields[field_name]
        example = json_format.MessageToDict(field.edge_config.example)
        parts = field_name.split(".")
        name = ".".join(parts[:-1])
        part = parts[-1]
        validate_fragment.validate_fragment(name, {part: example})
        result[field_name] = dict(note=field.edge_config.note, example=example)
    pathlib.Path(output).write_text(json.dumps(result))


if __name__ == "__main__":
    main(*sys.argv[1:])
