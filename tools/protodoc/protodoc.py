# protoc plugin to map from FileDescriptorProtos to Envoy doc style RST.
# See https://github.com/google/protobuf/blob/master/src/google/protobuf/descriptor.proto
# for the underlying protos mentioned in this file. See
# https://www.sphinx-doc.org/en/master/usage/restructuredtext/basics.html for Sphinx RST syntax.

import sys

# We have to do some evil things to sys.path due to the way that Python module
# resolution works; we have both tools/ trees in bazel_tools and envoy. By
# default, Bazel leaves us with a sys.path in which the @bazel_tools repository
# takes precedence. Now that we're done with importing runfiles above, we can
# just remove it from the sys.path.
sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]

from tools.api_proto_plugin import plugin
from tools.api_proto_plugin import visitor
from tools.docs.utils import rst_formatter

# Namespace prefix for Envoy core APIs.
ENVOY_API_NAMESPACE_PREFIX = '.envoy.api.v2.'

# Namespace prefix for Envoy top-level APIs.
ENVOY_PREFIX = '.envoy.'

# Namespace prefix for WKTs.
WKT_NAMESPACE_PREFIX = '.google.protobuf.'

# Namespace prefix for RPCs.
RPC_NAMESPACE_PREFIX = '.google.rpc.'

# http://www.fileformat.info/info/unicode/char/2063/index.htm
UNICODE_INVISIBLE_SEPARATOR = u'\u2063'


class ProtodocError(Exception):
    """Base error class for the protodoc module."""


def github_url(text, type_context):
    """Obtain data plane API Github URL by path from a TypeContext.

    Args:
        type_context: type_context.TypeContext for node.

    Returns:
        A string with a corresponding data plane API GitHub Url.
    """
    return f":repo:`{text} <api/{type_context.source_code_info.name}#L{type_context.location.span[0]}>`"


class RstFormatVisitor(visitor.Visitor):
    """Visitor to generate a RST representation from a FileDescriptor proto.

    See visitor.Visitor for visitor method docs comments.
    """

    def visit_enum(self, enum_proto, type_context):
        normal_enum_type = rst_formatter.pb.normalize_field_type_name(f".{type_context.name}")
        anchor = rst_formatter.anchor(rst_formatter.pb.cross_ref_label(normal_enum_type, "enum"))
        header = rst_formatter.header('Enum %s' % normal_enum_type, '-')
        proto_link = github_url(f"[{normal_enum_type} proto]", type_context) + '\n\n'
        leading_comment = type_context.leading_comment
        formatted_leading_comment = rst_formatter.pb.comment_with_annotations(leading_comment, 'enum')
        if rst_formatter.pb.hide_not_implemented(leading_comment):
            return ''
        return anchor + header + proto_link + formatted_leading_comment + rst_formatter.pb.enum_as_dl(
            type_context, enum_proto)

    def visit_message(self, msg_proto, type_context, nested_msgs, nested_enums):
        # Skip messages synthesized to represent map types.
        if msg_proto.options.map_entry:
            return ''
        normal_msg_type = rst_formatter.pb.normalize_field_type_name(f".{type_context.name}")
        anchor = rst_formatter.anchor(rst_formatter.pb.cross_ref_label(normal_msg_type, "msg"))
        header = rst_formatter.header(normal_msg_type, '-')
        proto_link = github_url(f"[{normal_msg_type} proto]", type_context) + '\n\n'
        leading_comment = type_context.leading_comment
        formatted_leading_comment = rst_formatter.pb.comment_with_annotations(leading_comment, 'message')
        if rst_formatter.pb.hide_not_implemented(leading_comment):
            return ''

        return anchor + header + proto_link + formatted_leading_comment + rst_formatter.pb.message_as_json(
            type_context, msg_proto) + rst_formatter.pb.message_as_dl(
                type_context, msg_proto) + '\n'.join(nested_msgs) + '\n' + '\n'.join(nested_enums)

    def visit_file(self, file_proto, type_context, services, msgs, enums):
        has_messages = True
        if all(len(msg) == 0 for msg in msgs) and all(len(enum) == 0 for enum in enums):
            has_messages = False

        v2_link = rst_formatter.v2_link(file_proto.name)

        # TODO(mattklein123): The logic in both the doc and transform tool around files without messages
        # is confusing and should be cleaned up. This is a stop gap to have titles for all proto docs
        # in the common case.
        if (has_messages and not rst_formatter.pb.annotations.DOC_TITLE_ANNOTATION
                in type_context.source_code_info.file_level_annotations
                and file_proto.name.startswith('envoy')):
            raise ProtodocError(
                'Envoy API proto file missing [#protodoc-title:] annotation: {}'.format(
                    file_proto.name))

        # Find the earliest detached comment, attribute it to file level.
        # Also extract file level titles if any.
        header, comment = rst_formatter.pb.header_from_file(
            '=', type_context.source_code_info, file_proto.name, v2_link)
        # If there are no messages, we don't include in the doc tree (no support for
        # service rendering yet). We allow these files to be missing from the
        # toctrees.
        if not has_messages:
            header = ':orphan:\n\n' + header
        warnings = ''
        if file_proto.options.HasExtension(rst_formatter.pb.status_pb2.file_status):
            if file_proto.options.Extensions[rst_formatter.pb.status_pb2.file_status].work_in_progress:
                warnings += (
                    '.. warning::\n   This API is work-in-progress and is '
                    'subject to breaking changes.\n\n')
        # debug_proto = format_proto_as_block_comment(file_proto)
        return header + warnings + comment + '\n'.join(msgs) + '\n'.join(enums)  # + debug_proto


def main():
    plugin.plugin([plugin.direct_output_descriptor('.rst', RstFormatVisitor)])


if __name__ == '__main__':
    main()
