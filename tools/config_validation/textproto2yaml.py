# Validate a YAML fragment against an Envoy API proto3 type.
#
# Example usage:
#
# bazel run //tools/config_validation:textproto2yaml -- --as_fragment \
#   envoy.config.bootstrap.v3.Bootstrap \
#   $PWD/tools/testdata/config_validation/input.textproto

import argparse
import json
import pathlib
import sys

import yaml

from google.protobuf import json_format
from google.protobuf import text_format

from common import load_pool
from common import IgnoredKey


def proto_message_to_yaml(proto_message, pool):
    yaml.SafeLoader.add_constructor('!ignore', IgnoredKey.from_yaml)
    yaml.SafeDumper.add_multi_representer(IgnoredKey, IgnoredKey.to_yaml)
    json_msg = json_format.MessageToJson(
        proto_message, descriptor_pool=pool, preserving_proto_field_name=True)
    return yaml.dump(json.loads(json_msg), sort_keys=False)


def textproto_file_to_yaml(textproto_path, message_type, *, descriptor_path=None) -> str:
    msg, pool = load_pool(message_type, descriptor_path=descriptor_path)
    content = pathlib.Path(textproto_path).read_text()
    proto_message = text_format.Parse(content, msg, descriptor_pool=pool)
    return proto_message_to_yaml(proto_message, pool)


def yaml_to_fragment(yaml_message, message_type) -> str:
    fragment = [
        '// .. validated-code-block:: yaml',
        f'//   :type-name: {message_type}',
        '//',
    ]
    for line in yaml_message.splitlines():
        fragment.append(f'//   {line}')
    fragment.append('//')
    return '\n'.join(fragment) + '\n'


def parse_args(args):
    parser = argparse.ArgumentParser(description='Print textproto as YAML.')
    parser.add_argument(
        'message_type',
        help=('a string providing the type name, '
              'e.g. envoy.config.bootstrap.v3.Bootstrap.'))
    parser.add_argument(
        'textproto_path', nargs='?', help='Path to a .textproto configuration fragment.')
    parser.add_argument('--descriptor_path', nargs='?', help='Path to a protobuf descriptor file.')
    parser.add_argument(
        '--as_fragment', action=argparse.BooleanOptionalAction, help='Print as a fragment.')
    return parser.parse_args(args)


def main(*args) -> int:
    parsed_args = parse_args(args)
    yaml_message = textproto_file_to_yaml(
        parsed_args.textproto_path,
        parsed_args.message_type,
        descriptor_path=parsed_args.descriptor_path)
    out = yaml_message if not parsed_args.as_fragment else yaml_to_fragment(
        yaml_message, parsed_args.message_type)
    sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
