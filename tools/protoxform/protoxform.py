# protoc plugin to map from FileDescriptorProtos to intermediate form
#
# protoxform takes a source FileDescriptorProto and generates active/next major
# version candidate FileDescriptorProtos. The resulting FileDescriptorProtos are
# then later processed by proto_sync.py, which invokes protoprint.py to format.

import functools

from tools.api_proto_plugin import plugin, visitor
from tools.protoxform import utils

from udpa.annotations import status_pb2


class ProtoXformError(Exception):
    """Base error class for the protoxform module."""


class ProtoFormatVisitor(visitor.Visitor):
    """Visitor to generate a proto representation from a FileDescriptor proto.

    See visitor.Visitor for visitor method docs comments.
    """

    def __init__(self, active_or_frozen, params):
        if params['type_db_path']:
            utils.load_type_db(params['type_db_path'])
        self._freeze = 'extra_args' in params and params['extra_args'] == 'freeze'
        self._active_or_frozen = active_or_frozen

    def visit_service(self, service_proto, type_context):
        return None

    def visit_enum(self, enum_proto, type_context):
        return None

    def visit_message(self, msg_proto, type_context, nested_msgs, nested_enums):
        return None

    def visit_file(self, file_proto, type_context, services, msgs, enums):
        # Freeze protos that have next major version candidates.
        typedb = utils.get_type_db()
        existing_pkg_version_status = file_proto.options.Extensions[
            status_pb2.file_status].package_version_status
        empty_file = len(services) == 0 and len(enums) == 0 and len(msgs) == 0
        pkg_version_status_exempt = (
            file_proto.name.startswith('envoy/annotations') or empty_file
            or file_proto.name.startswith('xds'))
        # It's a format error not to set package_version_status.
        if existing_pkg_version_status == status_pb2.UNKNOWN and not pkg_version_status_exempt:
            raise ProtoXformError('package_version_status must be set in %s' % file_proto.name)
        # Only update package_version_status for .active_or_frozen.proto,
        if self._active_or_frozen and not pkg_version_status_exempt:
            # Freeze if this is an active package with a next major version. Preserve
            # frozen status otherwise.
            if self._freeze and typedb.next_version_protos.get(file_proto.name, None):
                target_pkg_version_status = status_pb2.FROZEN
            elif existing_pkg_version_status == status_pb2.FROZEN:
                target_pkg_version_status = status_pb2.FROZEN
            else:
                assert (existing_pkg_version_status == status_pb2.ACTIVE)
                target_pkg_version_status = status_pb2.ACTIVE
            file_proto.options.Extensions[
                status_pb2.file_status].package_version_status = target_pkg_version_status
        return str(file_proto)


def main():
    utils.load_protos()

    plugin.plugin([
        plugin.direct_output_descriptor(
            '.active_or_frozen.proto',
            functools.partial(ProtoFormatVisitor, True),
            want_params=True),
    ])


if __name__ == '__main__':
    main()
