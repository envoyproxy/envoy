# Validate a YAML fragment against an Envoy API proto3 type.
#
# Example usage:
#
# bazel run //tools/config_validation:validate_fragment -- \
#   envoy.config.bootstrap.v3.Bootstrap $PWD/configs/google_com_proxy.v2.yaml

import json
import pathlib
import sys

import yaml

from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool
from google.protobuf import json_format
from google.protobuf import message_factory
from google.protobuf import text_format

from bazel_tools.tools.python.runfiles import runfiles


def ValidateFragment(type_name, fragment):
  """Validate a dictionary representing a JSON/YAML fragment against an Envoy API proto3 type.

  Throws Protobuf errors on parsing exceptions, successful validations produce
  no result.

  Args:
    type_name: a string providing the type name, e.g.
      envoy.config.bootstrap.v3.Bootstrap.
    fragment: a dictionary representing the parsed JSON/YAML configuration
      fragment.
  """
  json_fragment = json.dumps(fragment)

  r = runfiles.Create()
  all_protos_pb_text_path = r.Rlocation(
      'envoy/tools/type_whisperer/all_protos_with_ext_pb_text.pb_text')
  file_desc_set = descriptor_pb2.FileDescriptorSet()
  text_format.Parse(pathlib.Path(all_protos_pb_text_path).read_text(),
                    file_desc_set,
                    allow_unknown_extension=True)

  pool = descriptor_pool.DescriptorPool()
  for f in file_desc_set.file:
    pool.Add(f)
  desc = pool.FindMessageTypeByName(type_name)
  msg = message_factory.MessageFactory(pool=pool).GetPrototype(desc)()
  json_format.Parse(json_fragment, msg, descriptor_pool=pool)


if __name__ == '__main__':
  type_name, yaml_path = sys.argv[1:]
  ValidateFragment(type_name, yaml.load(pathlib.Path(yaml_path).read_text(),
                                        Loader=yaml.FullLoader))
