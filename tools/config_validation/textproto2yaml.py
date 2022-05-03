# Validate a YAML fragment against an Envoy API proto3 type.
#
# Example usage:
#
# bazel run //tools/config_validation:textproto2yaml -- \
#   envoy.config.bootstrap.v3.Bootstrap $PWD/configs/envoyproxy_io_proxy.textproto

import json
import pathlib

import yaml

from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool
from google.protobuf import json_format
from google.protobuf import message_factory
from google.protobuf import text_format

from bazel_tools.tools.python.runfiles import runfiles

import argparse


class IgnoredKey(yaml.YAMLObject):
    """Python support type for Envoy's config !ignore tag."""
    yaml_tag = '!ignore'

    def __init__(self, strval):
        self.strval = strval

    def __repr__(self):
        return f'IgnoredKey({str})'

    def __eq__(self, other):
        return isinstance(other, IgnoredKey) and self.strval == other.strval

    def __hash__(self):
        return hash((self.yaml_tag, self.strval))

    @classmethod
    def from_yaml(cls, loader, node):
        return IgnoredKey(node.value)

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_scalar(cls.yaml_tag, data.strval)


def load_pool(type_name, descriptor_path=None):
    if not descriptor_path:
        r = runfiles.Create()
        descriptor_path = r.Rlocation(
            'envoy/tools/type_whisperer/all_protos_with_ext_pb_text.pb_text')

    file_desc_set = descriptor_pb2.FileDescriptorSet()
    text_format.Parse(
        pathlib.Path(descriptor_path).read_text(), file_desc_set, allow_unknown_extension=True)

    pool = descriptor_pool.DescriptorPool()
    for f in file_desc_set.file:
        pool.Add(f)
    desc = pool.FindMessageTypeByName(type_name)
    msg = message_factory.MessageFactory(pool=pool).GetPrototype(desc)()

    return msg, pool


def textproto_to_yaml(in_textproto, pool):
    yaml.SafeLoader.add_constructor('!ignore', IgnoredKey.from_yaml)
    yaml.SafeDumper.add_multi_representer(IgnoredKey, IgnoredKey.to_yaml)
    json_msg = json_format.MessageToJson(
      in_textproto,
      descriptor_pool=pool,
      preserving_proto_field_name=True)
    # print(in_textproto)
    # print("----")
    # print(json_msg)
    # print("----")
    return yaml.dump(json.loads(json_msg), sort_keys=False)


def parse_args():
    parser = argparse.ArgumentParser(description='Print textproto as YAML.')
    parser.add_argument(
        'message_type',
        help='a string providing the type name, e.g. envoy.config.bootstrap.v3.Bootstrap.')
    parser.add_argument('textproto_path', nargs='?', help='Path to a .textproto configuration fragment.')
    parser.add_argument('--descriptor_path', nargs='?', help='Path to a protobuf descriptor file.')
    parser.add_argument('--as_fragment', action=argparse.BooleanOptionalAction, help='Print as a fragment.')
    return parser.parse_args()


def main(message_type, descriptor_path, content, *, as_fragment=False):
  msg, pool = load_pool(message_type, descriptor_path=descriptor_path)
  in_textproto = text_format.Parse(content, msg, descriptor_pool=pool)
  out_yaml: str = textproto_to_yaml(in_textproto, pool)

  if not as_fragment:
    return out_yaml.rstrip('\n')

  fragment = [
    '// .. validated-code-block:: yaml',
    f'//   :type-name: {message_type}',
    '//',
  ]
  for line in out_yaml.splitlines():
    fragment.append(f'//   {line}')

  fragment.append('//')
  return '\n'.join(fragment)


if __name__ == '__main__':
    parsed_args = parse_args()
    out = main(parsed_args.message_type,
         parsed_args.descriptor_path,
         pathlib.Path(parsed_args.textproto_path).read_text(),
         as_fragment=parsed_args.as_fragment)

    print(out,)
