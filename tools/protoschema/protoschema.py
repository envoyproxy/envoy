# protoc plugin to map from FileDescriptorProtos to Envoy doc style RST.

# We have to do some evil things to sys.path due to the way that Python module
# resolution works; we have both tools/ trees in bazel_tools and envoy. By
# default, Bazel leaves us with a sys.path in which the @bazel_tools repository
# takes precedence. Now that we're done with importing runfiles above, we can
# just remove it from the sys.path.

import sys

sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]

from tools.api_proto_plugin import plugin
from tools.api_proto_plugin import visitor


class JSONSchemaFormatVisitor(visitor.Visitor):
  """Visitor to generate a RST representation from a FileDescriptor proto.

  See visitor.Visitor for visitor method docs comments.
  """

  def __init__(self):
    pass

  def VisitEnum(self, enum_proto, type_context):
    return ''

  def VisitMessage(self, msg_proto, type_context, nested_msgs, nested_enums):
    return ''

  def VisitFile(self, file_proto, type_context, services, msgs, enums):
    return ''


def Main():
  plugin.Plugin([plugin.DirectOutputDescriptor('.rst', JSONSchemaFormatVisitor)])


if __name__ == '__main__':
  Main()
