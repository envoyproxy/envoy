# protoc plugin to map from FileDescriptorProtos to intermediate form
#
# protoxform takes a source FileDescriptorProto and generates active/next major
# version candidate FileDescriptorProtos. The resulting FileDescriptorProtos are
# then later processed by proto_sync.py, which invokes protoprint.py to format.

import functools

from tools.api_proto_plugin import plugin
from tools.api_proto_plugin import visitor
from tools.protoxform import migrate
from tools.protoxform import utils

# Note: we have to include those proto definitions to ensure we don't lose these
# during FileDescriptorProto printing.
from google.api import annotations_pb2 as _
from validate import validate_pb2 as _
from envoy.annotations import deprecation_pb2 as _
from envoy.annotations import resource_pb2
from udpa.annotations import migrate_pb2
from udpa.annotations import sensitive_pb2 as _
from udpa.annotations import status_pb2


class ProtoFormatVisitor(visitor.Visitor):
  """Visitor to generate a proto representation from a FileDescriptor proto.

  See visitor.Visitor for visitor method docs comments.
  """

  def __init__(self, pkg_version_status):
    self._pkg_version_status = pkg_version_status

  def VisitService(self, service_proto, type_context):
    return None

  def VisitEnum(self, enum_proto, type_context):
    return None

  def VisitMessage(self, msg_proto, type_context, nested_msgs, nested_enums):
    return None

  def VisitFile(self, file_proto, type_context, services, msgs, enums):
    file_proto.options.Extensions[
        status_pb2.file_status].package_version_status = self._pkg_version_status
    return str(file_proto)


def ParameterCallback(parameter):
  params = dict(param.split('=') for param in parameter.split(','))
  if params['type_db_path']:
    utils.LoadTypeDb(params['type_db_path'])


def Main():
  plugin.Plugin([
      plugin.DirectOutputDescriptor('.active.proto',
                                    functools.partial(ProtoFormatVisitor, status_pb2.ACTIVE)),
      plugin.OutputDescriptor(
          '.next_major_version_candidate.proto',
          functools.partial(ProtoFormatVisitor, status_pb2.NEXT_MAJOR_VERSION_CANDIDATE),
          functools.partial(migrate.VersionUpgradeXform, 2, False)),
      plugin.OutputDescriptor(
          '.next_major_version_candidate.envoy_internal.proto',
          functools.partial(ProtoFormatVisitor, status_pb2.NEXT_MAJOR_VERSION_CANDIDATE),
          functools.partial(migrate.VersionUpgradeXform, 2, True))
  ], ParameterCallback)


if __name__ == '__main__':
  Main()
