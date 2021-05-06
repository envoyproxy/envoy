# protoc plugin to map from FileDescriptorProtos to Envoy doc style RST.
# See https://github.com/google/protobuf/blob/master/src/google/protobuf/descriptor.proto
# for the underlying protos mentioned in this file. See
# https://www.sphinx-doc.org/en/master/usage/restructuredtext/basics.html for Sphinx RST syntax.

from collections import defaultdict
import json
import functools
import os
import pathlib
import sys

from google.protobuf import json_format
from bazel_tools.tools.python.runfiles import runfiles
import yaml

from jinja2 import Template

# We have to do some evil things to sys.path due to the way that Python module
# resolution works; we have both tools/ trees in bazel_tools and envoy. By
# default, Bazel leaves us with a sys.path in which the @bazel_tools repository
# takes precedence. Now that we're done with importing runfiles above, we can
# just remove it from the sys.path.
sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]

from tools.api_proto_plugin import annotations
from tools.api_proto_plugin import plugin
from tools.api_proto_plugin import visitor
from tools.config_validation import validate_fragment

from tools.protodoc import manifest_pb2
from udpa.annotations import security_pb2
from udpa.annotations import status_pb2
from validate import validate_pb2

# Namespace prefix for Envoy core APIs.
ENVOY_API_NAMESPACE_PREFIX = '.envoy.api.v2.'

# Last documented v2 api version
ENVOY_LAST_V2_VERSION = "1.17.2"

# Namespace prefix for Envoy top-level APIs.
ENVOY_PREFIX = '.envoy.'

# Namespace prefix for WKTs.
WKT_NAMESPACE_PREFIX = '.google.protobuf.'

# Namespace prefix for RPCs.
RPC_NAMESPACE_PREFIX = '.google.rpc.'

# http://www.fileformat.info/info/unicode/char/2063/index.htm
UNICODE_INVISIBLE_SEPARATOR = u'\u2063'

# Template for data plane API URLs.
DATA_PLANE_API_URL_FMT = 'https://github.com/envoyproxy/envoy/blob/{}/api/%s#L%d'.format(
    os.environ['ENVOY_BLOB_SHA'])

# Template for formating an extension category.
EXTENSION_CATEGORY_TEMPLATE = Template(
    """
.. _extension_category_{{category}}:

.. tip::
  This extension category has the following known extensions:

{% for ext in extensions %}
  - :ref:`{{ext}} <extension_{{ext}}>`
{% endfor %}

""")

# A map from the extension security postures (as defined in the
# envoy_cc_extension build macro) to human readable text for extension docs.
EXTENSION_SECURITY_POSTURES = {
    'robust_to_untrusted_downstream':
        'This extension is intended to be robust against untrusted downstream traffic. It '
        'assumes that the upstream is trusted.',
    'robust_to_untrusted_downstream_and_upstream':
        'This extension is intended to be robust against both untrusted downstream and '
        'upstream traffic.',
    'requires_trusted_downstream_and_upstream':
        'This extension is not hardened and should only be used in deployments'
        ' where both the downstream and upstream are trusted.',
    'unknown':
        'This extension has an unknown security posture and should only be '
        'used in deployments where both the downstream and upstream are '
        'trusted.',
    'data_plane_agnostic':
        'This extension does not operate on the data plane and hence is intended to be robust against untrusted traffic.',
}

# A map from the extension status value to a human readable text for extension
# docs.
EXTENSION_STATUS_VALUES = {
    'alpha':
        'This extension is functional but has not had substantial production burn time, use only with this caveat.',
    'wip':
        'This extension is work-in-progress. Functionality is incomplete and it is not intended for production use.',
}

EXTENSION_DB = json.loads(pathlib.Path(os.getenv('EXTENSION_DB_PATH')).read_text())

# create an index of extension categories from extension db
EXTENSION_CATEGORIES = {}
for _k, _v in EXTENSION_DB.items():
    for _cat in _v['categories']:
        EXTENSION_CATEGORIES.setdefault(_cat, []).append(_k)


class ProtodocError(Exception):
    """Base error class for the protodoc module."""


def hide_not_implemented(comment):
    """Should a given type_context.Comment be hidden because it is tagged as [#not-implemented-hide:]?"""
    return annotations.NOT_IMPLEMENTED_HIDE_ANNOTATION in comment.annotations


def github_url(type_context):
    """Obtain data plane API Github URL by path from a TypeContext.

    Args:
        type_context: type_context.TypeContext for node.

    Returns:
        A string with a corresponding data plane API GitHub Url.
    """
    if type_context.location is not None:
        return DATA_PLANE_API_URL_FMT % (
            type_context.source_code_info.name, type_context.location.span[0])
    return ''


def parse_comment(comment, type_name=''):
    return dict(
        comment=annotations.without_annotations(strip_leading_space(comment.raw)),
        extension=get_extension(comment.annotations.get(annotations.EXTENSION_ANNOTATION)),
        extension_categories=get_extension_categories(comment.annotations.get(annotations.EXTENSION_CATEGORY_ANNOTATION, "").split(",")))


def map_lines(f, s):
    """Apply a function across each line in a flat string.

    Args:
        f: A string transform function for a line.
        s: A string consisting of potentially multiple lines.

    Returns:
        A flat string with f applied to each line.
    """
    return '\n'.join(f(line) for line in s.split('\n'))


def indent(spaces, line):
    """Indent a string."""
    return ' ' * spaces + line


def indent_lines(spaces, lines):
    """Indent a list of strings."""
    return map(functools.partial(indent, spaces), lines)


def format_internal_link(text, ref):
    return ':ref:`%s <%s>`' % (text, ref)


def format_external_link(text, ref):
    return '`%s <%s>`_' % (text, ref)


def format_header(style, text):
    """Format RST header.

    Args:
        style: underline style, e.g. '=', '-'.
        text: header text

    Returns:
        RST formatted header.
    """
    return '%s\n%s\n\n' % (text, style * len(text))


def get_extension(extension):
    """Format extension metadata as RST.

    Args:
        extension: the name of the extension, e.g. com.acme.foo.

    Returns:
        RST formatted extension description.
    """
    if not extension:
        return {}
    try:
        extension_metadata = EXTENSION_DB[extension]
        status = EXTENSION_STATUS_VALUES.get(extension_metadata['status'], '')
        security_posture = EXTENSION_SECURITY_POSTURES[extension_metadata['security_posture']]
        categories = extension_metadata["categories"]
    except KeyError as e:
        sys.stderr.write(
            f"\n\nDid you forget to add '{extension}' to source/extensions/extensions_build_config.bzl?\n\n"
        )
        exit(1)  # Raising the error buries the above message in tracebacks.

    return dict(
        name=extension,
        status=status,
        security_posture=security_posture,
        categories=categories)


def get_extension_categories(categories):
    try:
        return [
            (category, EXTENSION_CATEGORIES[category])
            for category in categories
            if category.strip()]
    except KeyError as e:
        raise ProtodocError(f"\n\nUnable to find extension category:  {e.args[0]}\n\n")


def format_extension_category(extension_category):
    """Format extension metadata as RST.

    Args:
        extension_category: the name of the extension_category, e.g. com.acme.

    Returns:
        RST formatted extension category description.
    """
    try:
        extensions = EXTENSION_CATEGORIES[extension_category]
    except KeyError as e:
        raise ProtodocError(f"\n\nUnable to find extension category:  {extension_category}\n\n")
    return EXTENSION_CATEGORY_TEMPLATE.render(
        category=extension_category, extensions=sorted(extensions))


def format_field_type_as_json(type_context, field):
    """Format FieldDescriptorProto.Type as a pseudo-JSON string.

    Args:
        type_context: contextual information for message/enum/field.
        field: FieldDescriptor proto.
    Return: RST formatted pseudo-JSON string representation of field type.
    """
    if type_name_from_fqn(field.type_name) in type_context.map_typenames:
        return "{...}"
    if field.label == field.LABEL_REPEATED:
        return []
    if field.type == field.TYPE_MESSAGE:
        return "{...}"
    return "..."


def normalize_field_type_name(field_fqn):
    """Normalize a fully qualified field type name, e.g.

    .envoy.foo.bar.

    Strips leading ENVOY_API_NAMESPACE_PREFIX and ENVOY_PREFIX.

    Args:
        field_fqn: a fully qualified type name from FieldDescriptorProto.type_name.
    Return: Normalized type name.
    """
    if field_fqn.startswith(ENVOY_API_NAMESPACE_PREFIX):
        return field_fqn[len(ENVOY_API_NAMESPACE_PREFIX):]
    if field_fqn.startswith(ENVOY_PREFIX):
        return field_fqn[len(ENVOY_PREFIX):]
    return field_fqn


def normalize_type_context_name(type_name):
    """Normalize a type name, e.g.

    envoy.foo.bar.

    Strips leading ENVOY_API_NAMESPACE_PREFIX and ENVOY_PREFIX.

    Args:
        type_name: a name from a TypeContext.
    Return: Normalized type name.
    """
    return normalize_field_type_name(qualify_type_name(type_name))


def qualify_type_name(type_name):
    return '.' + type_name


def type_name_from_fqn(fqn):
    return fqn[1:]


def strip_leading_space(s):
    """Remove leading space in flat comment strings."""
    return map_lines(lambda s: s[1:], s)


def field_cross_ref_label(field_name):
    """Field cross reference label."""
    return 'envoy_api_field_%s' % field_name


def format_anchor(label):
    """Format a label as an Envoy API RST anchor."""
    return '.. _%s:\n\n' % label


class RstFormatVisitor(visitor.Visitor):
    """Visitor to generate a RST representation from a FileDescriptor proto.

    See visitor.Visitor for visitor method docs comments.
    """

    def __init__(self):
        r = runfiles.Create()

        with open(r.Rlocation('envoy/docs/v2_mapping.json'), 'r') as f:
            self.v2_mapping = json.load(f)

        with open(r.Rlocation('envoy/tools/protodoc/protodoc.template.rst'), 'r') as f:
            self.template = Template(f.read())

        with open(r.Rlocation('envoy/docs/protodoc_manifest.yaml'), 'r') as f:
            # Load as YAML, emit as JSON and then parse as proto to provide type
            # checking.
            protodoc_manifest_untyped = yaml.safe_load(f.read())
            self.protodoc_manifest = manifest_pb2.Manifest()
            json_format.Parse(json.dumps(protodoc_manifest_untyped), self.protodoc_manifest)

    def get_v2_link(self, proto_name):
        if proto_name not in self.v2_mapping:
            return {}
        # TODO(phlax): remove _v2_ from filepath once sed mangling is removed
        v2_filepath = f"envoy_v2_api_file_{self.v2_mapping[proto_name]}"
        return dict(
            text=v2_filepath.split('/', 1)[1],
            url=f"v{ENVOY_LAST_V2_VERSION}:{v2_filepath}")

    def get_comment(self, comment):
        stripped = annotations.without_annotations('\n'.join(comment))
        return "\n".join(
            c[1:]
            for c in stripped.split("\n"))[1:]

    def get_field(self, outer_type_context, type_context, field):
        if hide_not_implemented(type_context.leading_comment):
            return {}

        required = False

        if field.options.HasExtension(validate_pb2.rules):
            rule = field.options.Extensions[validate_pb2.rules]
            if ((rule.HasField('message') and rule.message.required)
                or (rule.HasField('duration') and rule.duration.required)
                or (rule.HasField('string') and rule.string.min_len > 0)
                or (rule.HasField('string') and rule.string.min_bytes > 0)
                or (rule.HasField('repeated') and rule.repeated.min_items > 0)):
                required = True

        oneof = {}
        if field.HasField('oneof_index'):
            oneof_context = outer_type_context.extend_oneof(
                field.oneof_index, type_context.oneof_names[field.oneof_index])
            if hide_not_implemented(oneof_context.leading_comment):
                return {}

            # If the oneof only has one field and marked required, mark the field as required.
            if len(type_context.oneof_fields[field.oneof_index]) == 1 and type_context.oneof_required[field.oneof_index]:
                required = True

            if len(type_context.oneof_fields[field.oneof_index]) > 1:
                # Fields in oneof shouldn't be marked as required when we have oneof comment below it.
                required = False
                oneof = parse_comment(oneof_context.leading_comment)
                oneof["required"] = type_context.oneof_required[field.oneof_index]
                oneof["links"] = [
                    format_internal_link(
                        f,
                        field_cross_ref_label(
                            normalize_type_context_name(outer_type_context.extend_field(i, f).name)))
                    for i, f in type_context.oneof_fields[field.oneof_index]]

        return dict(
            proto=normalize_type_context_name(type_context.name),
            name=field.name.strip(),
            type=self.get_field_type(type_context, field),
            label=dict(
                repeated=field.label == field.LABEL_REPEATED,
                required=required),
            info=parse_comment(type_context.leading_comment),
            oneof=oneof,
            security_options=self.get_security_options(field, type_context))

    def get_primitive_field_type(self, type_context, field):
        primitive_types = {
            field.TYPE_DOUBLE: 'double',
            field.TYPE_FLOAT: 'float',
            field.TYPE_INT32: 'int32',
            field.TYPE_SFIXED32: 'int32',
            field.TYPE_SINT32: 'int32',
            field.TYPE_FIXED32: 'uint32',
            field.TYPE_UINT32: 'uint32',
            field.TYPE_INT64: 'int64',
            field.TYPE_SFIXED64: 'int64',
            field.TYPE_SINT64: 'int64',
            field.TYPE_FIXED64: 'uint64',
            field.TYPE_UINT64: 'uint64',
            field.TYPE_BOOL: 'bool',
            field.TYPE_STRING: 'string',
            field.TYPE_BYTES: 'bytes',
        }
        if field.type in primitive_types:
            return dict(
                text=primitive_types[field.type],
                type="external",
                url='https://developers.google.com/protocol-buffers/docs/proto#scalar')

    def get_field_type(self, type_context, field):
        field_type = (
            self.get_typed_field_type(type_context, field)
            if field.type_name
            else self.get_primitive_field_type(type_context, field))
        if not field_type:
            raise ProtodocError('Unknown field type ' + str(field.type))
        return field_type

    def get_security_options(self, field, type_context):
        # If there is a udpa.annotations.security option, include it after the comment.
        if not field.options.HasExtension(security_pb2.security):
            return {}
        manifest_description = self.protodoc_manifest.fields.get(type_context.name)
        if not manifest_description:
            raise ProtodocError('Missing protodoc manifest YAML for %s' % type_context.name)

        security_options = {}
        security_options["options"] = field.options.Extensions[security_pb2.security]
        edge_config = manifest_description.edge_config
        security_options["edge_config"] = edge_config.note
        example_dict = json_format.MessageToDict(edge_config.example)
        # validate_fragment.validate_fragment(field.type_name[1:], example_dict)
        security_options["example"] = {type_context.name.split('.')[-1]: example_dict}
        return security_options

    def get_typed_field_type(self, type_context, field):
        """Format a FieldDescriptorProto type description.

        Adds cross-refs for message types.
        TODO(htuch): Add cross-refs for enums as well.
        """
        type_name = field.type_name
        if type_name.startswith(ENVOY_API_NAMESPACE_PREFIX) or type_name.startswith(ENVOY_PREFIX):
            type_name = normalize_field_type_name(type_name)
            if field.type == field.TYPE_MESSAGE:
                if type_context.map_typenames and type_name_from_fqn(
                        type_name) in type_context.map_typenames:
                    return dict(text='map<%s, %s>' % tuple(
                        map(
                            functools.partial(self.get_field_type, type_context),
                            type_context.map_typenames[type_name_from_fqn(type_name)])))
                return dict(text=type_name, type="api_msg")
            if field.type == field.TYPE_ENUM:
                return dict(text=type_name, type="api_enum")
        elif type_name.startswith(WKT_NAMESPACE_PREFIX):
            wkt = type_name[len(WKT_NAMESPACE_PREFIX):]
            return dict(
                type="external",
                text=wkt,
                url=f"https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#{wkt.lower()}")
        elif type_name.startswith(RPC_NAMESPACE_PREFIX):
            rpc = type_name[len(RPC_NAMESPACE_PREFIX):]
            return dict(
                type="external",
                text=rpc,
                url=f"https://cloud.google.com/natural-language/docs/reference/rpc/google.rpc#{rpc.lower()}")
        elif type_name:
            return dict(text=type_name)

    def get_file(self, source_code_info, file_proto):
        return dict(
            proto=file_proto.name,
            title=source_code_info.file_level_annotations.get(annotations.DOC_TITLE_ANNOTATION),
            extension=get_extension(
                source_code_info.file_level_annotations.get(annotations.EXTENSION_ANNOTATION)),
            comment=self.get_comment(source_code_info.file_level_comments),
            work_in_progress=getattr(
                file_proto.options.Extensions.__getitem__(status_pb2.file_status) or object,
                "work_in_progress", False),
            v2_link=self.get_v2_link(file_proto.name))

    def get_fields(self, type_context, msg):
        type_context.oneof_fields = defaultdict(list)
        type_context.oneof_required = defaultdict(bool)
        type_context.oneof_names = defaultdict(list)
        for index, field in enumerate(msg.field):
            if field.HasField('oneof_index'):
                leading_comment = type_context.extend_field(index, field.name).leading_comment
                if hide_not_implemented(leading_comment):
                    continue
                type_context.oneof_fields[field.oneof_index].append((index, field.name))
        for index, oneof_decl in enumerate(msg.oneof_decl):
            if oneof_decl.options.HasExtension(validate_pb2.required):
                type_context.oneof_required[index] = oneof_decl.options.Extensions[
                    validate_pb2.required]
            type_context.oneof_names[index] = oneof_decl.name
        return [
            self.get_field(type_context, type_context.extend_field(index, field.name), field)
            for index, field in enumerate(msg.field)]

    def get_message_config(self, type_context, msg):
        data = {}
        for index, field in enumerate(msg.field):
            field_type_context = type_context.extend_field(index, field.name)
            leading_comment = field_type_context.leading_comment
            if hide_not_implemented(leading_comment):
                continue
            data[field.name] = format_field_type_as_json(type_context, field)
        return data

    def visit_enum(self, enum_proto, type_context):
        if hide_not_implemented(type_context.leading_comment):
            return ''
        normal_enum_type = normalize_type_context_name(type_context.name)
        return dict(
            proto=normal_enum_type,
            proto_link=dict(text=normal_enum_type, url=github_url(type_context)),
            comment=parse_comment(type_context.leading_comment, 'enum'),
            values=self.get_enum_values(type_context, enum_proto))

    def get_enum_values(self, type_context, enum):
        values = {}
        for (index, enum_value) in enumerate(enum.value):
            ctx = type_context.extend_enum_value(index, enum_value.name)
            if hide_not_implemented(ctx.leading_comment):
                continue
            default_comment = '*(DEFAULT)* ' if enum_value.number == 0 else ''
            comment = default_comment + UNICODE_INVISIBLE_SEPARATOR + str(parse_comment(ctx.leading_comment))
            values[enum_value.name] = dict(
                anchor=normalize_type_context_name(type_context.name),
                name=enum_value.name,
                comment=comment)
        return values

    def visit_message(self, msg_proto, type_context, nested_msgs, nested_enums):
        # Skip messages synthesized to represent map types.
        if msg_proto.options.map_entry or hide_not_implemented(type_context.leading_comment):
            return {}
        normal_msg_type = normalize_type_context_name(type_context.name)
        data = parse_comment(type_context.leading_comment, 'message')
        data.update(
            dict(proto=normal_msg_type,
                 proto_link=dict(text=f"[{normal_msg_type} proto]", url=github_url(type_context)),
                 config=self.get_message_config(type_context, msg_proto),
                 fields=self.get_fields(type_context, msg_proto),
                 messages=nested_msgs,
                 enums=nested_enums))
        return data

    def visit_file(self, file_proto, type_context, services, msgs, enums):
        data = dict(
            json=json,
            yaml=yaml,
            file=self.get_file(type_context.source_code_info, file_proto),
            has_messages=bool(sum(len(item) for item in msgs + enums) > 0),
            msgs=msgs,
            enums=enums)
        missing_title = (
            data["has_messages"]
            and not data["file"]["title"]
            and file_proto.name.startswith('envoy'))
        if missing_title:
            raise ProtodocError(
                f"Envoy API proto file missing [#protodoc-title:] annotation: {file_proto.name}")
        return self.template.render(data, trim_blocks=True, lstrip_blocks=True)


def main():
    plugin.plugin([plugin.direct_output_descriptor('.rst', RstFormatVisitor)])


if __name__ == '__main__':
    main()
