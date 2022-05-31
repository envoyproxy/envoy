# Common tools for building and validating Envoy doc fragments.

import pathlib

import yaml

from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool
from google.protobuf import message_factory
from google.protobuf import text_format

from bazel_tools.tools.python.runfiles import runfiles


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
