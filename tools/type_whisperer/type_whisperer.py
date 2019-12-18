# protoc plugin to map from FileDescriptorProtos to a tools.type_whisperer.Types
# proto. This is the type information for a single .proto, consumed by
# typedb_gen.py.

from tools.api_proto_plugin import plugin
from tools.api_proto_plugin import visitor

from tools.type_whisperer.types_pb2 import Types

from udpa.annotations import migrate_pb2


class TypeWhispererVisitor(visitor.Visitor):
  """Visitor to compute type information from a FileDescriptor proto.

  See visitor.Visitor for visitor method docs comments.
  """

  def __init__(self):
    super(TypeWhispererVisitor, self).__init__()
    self._types = Types()

  def VisitService(self, service_proto, type_context):
    pass

  def VisitEnum(self, enum_proto, type_context):
    type_desc = self._types.types[type_context.name]
    type_desc.type_details.enum_type = True
    for v in enum_proto.value:
      if v.options.deprecated:
        type_desc.next_version_upgrade = True
        type_desc.type_details.names[v.name].deprecate = True
      if v.options.HasExtension(migrate_pb2.enum_value_migrate):
        type_desc.type_details.names[v.name].rename = v.options.Extensions[
            migrate_pb2.enum_value_migrate].rename

  def VisitMessage(self, msg_proto, type_context, nested_msgs, nested_enums):
    type_desc = self._types.types[type_context.name]
    type_deps = set([])
    for f in msg_proto.field:
      if f.type_name.startswith('.'):
        type_deps.add(f.type_name[1:])
      if f.options.deprecated:
        type_desc.next_version_upgrade = True
        type_desc.type_details.names[f.name].deprecate = True
      if f.options.HasExtension(migrate_pb2.field_migrate):
        type_desc.type_details.names[f.name].rename = f.options.Extensions[
            migrate_pb2.field_migrate].rename

    type_desc.type_dependencies.extend(type_deps)

  def VisitFile(self, file_proto, type_context, services, msgs, enums):
    for t in self._types.types.values():
      t.type_details.qualified_package = file_proto.package
      t.type_details.proto_path = file_proto.name
    # Return in text proto format. This makes things easier to debug, these
    # don't need to be compact as they are only interim build artifacts.
    return str(self._types)


def Main():
  plugin.Plugin([
      plugin.DirectOutputDescriptor('.types.pb_text', TypeWhispererVisitor),
  ])


if __name__ == '__main__':
  Main()
