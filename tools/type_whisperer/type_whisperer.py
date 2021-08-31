# protoc plugin to map from FileDescriptorProtos to a tools.type_whisperer.Types
# proto. This is the type information for a single .proto, consumed by
# typedb_gen.py.

from tools.api_proto_plugin import plugin
from tools.api_proto_plugin import visitor

from tools.type_whisperer.types_pb2 import Types
from udpa.annotations import migrate_pb2
from udpa.annotations import status_pb2


class TypeWhispererVisitor(visitor.Visitor):
    """Visitor to compute type information from a FileDescriptor proto.

    See visitor.Visitor for visitor method docs comments.
    """

    def __init__(self):
        super(TypeWhispererVisitor, self).__init__()
        self._types = Types()

    def visit_service(self, service_proto, type_context):
        pass

    def visit_enum(self, enum_proto, type_context):
        type_desc = self._types.types[type_context.name]
        type_desc.next_version_upgrade = any(v.options.deprecated for v in enum_proto.value)
        type_desc.deprecated_type = type_context.deprecated

    def visit_message(self, msg_proto, type_context, nested_msgs, nested_enums):
        type_desc = self._types.types[type_context.name]
        type_desc.map_entry = msg_proto.options.map_entry
        type_desc.deprecated_type = type_context.deprecated
        type_deps = set([])
        for f in msg_proto.field:
            if f.type_name.startswith('.'):
                type_deps.add(f.type_name[1:])
            if f.options.deprecated:
                type_desc.next_version_upgrade = True
        type_desc.type_dependencies.extend(type_deps)

    def visit_file(self, file_proto, type_context, services, msgs, enums):
        next_version_package = ''
        if file_proto.options.HasExtension(migrate_pb2.file_migrate):
            next_version_package = file_proto.options.Extensions[
                migrate_pb2.file_migrate].move_to_package
        for t in self._types.types.values():
            t.qualified_package = file_proto.package
            t.proto_path = file_proto.name
            t.active = file_proto.options.Extensions[
                status_pb2.file_status].package_version_status == status_pb2.ACTIVE
            if next_version_package:
                t.next_version_package = next_version_package
                t.next_version_upgrade = True
        # Return in text proto format. This makes things easier to debug, these
        # don't need to be compact as they are only interim build artifacts.
        return str(self._types)


def main():
    plugin.plugin([
        plugin.direct_output_descriptor('.types.pb_text', TypeWhispererVisitor),
    ])


if __name__ == '__main__':
    main()
