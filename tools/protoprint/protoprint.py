# FileDescriptorProtos pretty-printer tool.
#
# protoprint.py provides the canonical .proto formatting for the Envoy APIs.
#
# See https://github.com/google/protobuf/blob/master/src/google/protobuf/descriptor.proto
# for the underlying protos mentioned in this file.
#
# Usage: protoprint.py <source file path> <type database path> <load type db path>
#                      <api version file path>

import copy
import functools
import io
import os
import pathlib
import re
import shutil
import subprocess
from collections import deque
from functools import cached_property

from packaging import version

from tools.api_proto_plugin import annotations, constants, plugin, traverse, visitor
from tools.api_versioning import utils as api_version_utils
from tools.protoxform import options as protoxform_options, utils
from tools.type_whisperer import type_whisperer, types_pb2

from google.protobuf import descriptor_pb2
from google.protobuf import text_format

from envoy.annotations import deprecation_pb2
from udpa.annotations import migrate_pb2, status_pb2
from xds.annotations.v3 import status_pb2 as xds_status_pb2

import envoy_repo

NEXT_FREE_FIELD_MIN = 5

ENVOY_DEPRECATED_UNAVIALABLE_NAME = 'DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE'


class ProtoPrintError(Exception):
    """Base error class for the protoprint module."""


def extract_clang_proto_style(clang_format_text):
    """Extract a key:value dictionary for proto formatting.

    Args:
        clang_format_text: text from a .clang-format file.

    Returns:
        key:value dictionary suitable for passing to clang-format --style.
    """
    lang = None
    format_dict = {}
    for line in clang_format_text.split('\n'):
        if lang is None or lang != 'Proto':
            match = re.match('Language:\s+(\w+)', line)
            if match:
                lang = match.group(1)
            continue
        match = re.match('(\w+):\s+(\w+)', line)
        if match:
            key, value = match.groups()
            format_dict[key] = value
        else:
            break
    return str(format_dict)


def clang_format(style, contents):
    """Run proto-style oriented clang-format over given string.

    Args:
        contents: a string with proto contents.

    Returns:
        clang-formatted string
    """
    clang_format_path = os.getenv("CLANG_FORMAT", shutil.which("clang-format"))
    if not clang_format_path:
        if not os.path.exists("/opt/llvm/bin/clang-format"):
            raise RuntimeError("Unable to find clang-format, sorry")
        clang_format_path = "/opt/llvm/bin/clang-format"
    return subprocess.run(
        [clang_format_path, '--style=%s' % style, '--assume-filename=.proto'],
        input=contents.encode('utf-8'),
        stdout=subprocess.PIPE).stdout


def format_block(block):
    """Append \n to a .proto section (e.g.

    comment, message definition, etc.) if non-empty.

    Args:
        block: a string representing the section.

    Returns:
        A string with appropriate whitespace.
    """
    if block.strip():
        return block + '\n'
    return ''


# TODO(htuch): not sure why this is needed, but clang-format does some weird
# stuff with // comment indents when we have these trailing \
def fixup_trailing_backslash(s):
    return s[:-1].rstrip() if s.endswith('\\') else s


def format_comments(comments):
    """Format a list of comment blocks from SourceCodeInfo.

    Prefixes // to each line, separates blocks by spaces.

    Args:
        comments: a list of blocks, each block is a list of strings representing
           lines in each block.

    Returns:
        A string reprenting the formatted comment blocks.
    """

    return format_block(
        '\n\n'.join(
            '\n'.join(
                ['// %s' % fixup_trailing_backslash(line)
                 for line in comment.split('\n')[:-1]])
            for comment in comments))


def create_next_free_field_xform(msg_proto):
    """Return the next free field number annotation transformer of a message.

    Args:
        msg_proto: DescriptorProto for message.

    Returns:
        the next free field number annotation transformer.
    """
    next_free = max(
        sum([
            [f.number + 1 for f in msg_proto.field],
            [rr.end for rr in msg_proto.reserved_range],
            [ex.end for ex in msg_proto.extension_range],
        ], [1]))
    return lambda _: next_free if next_free > NEXT_FREE_FIELD_MIN else None


def format_type_context_comments(type_context, annotation_xforms=None):
    """Format the leading/trailing comments in a given TypeContext.

    Args:
        type_context: contextual information for message/enum/field.
        annotation_xforms: a dict of transformers for annotations in leading
           comment.

    Returns:
        Tuple of formatted leading and trailing comment blocks.
    """
    leading_comment = type_context.leading_comment
    if annotation_xforms:
        leading_comment = leading_comment.get_comment_with_transforms(annotation_xforms)
    leading = format_comments(list(type_context.leading_detached_comments) + [leading_comment.raw])
    trailing = format_block(format_comments([type_context.trailing_comment]))
    return leading, trailing


def format_header_from_file(
        source_code_info, file_proto, empty_file, requires_deprecation_annotation):
    """Format proto header.

    Args:
        source_code_info: SourceCodeInfo object.
        file_proto: FileDescriptorProto for file.
        empty_file: are there no message/enum/service defs in file?
        requires_deprecation_annotation: does the proto have the deprecated version annotation or
                                         disallowed annotation.

    Returns:
        Formatted proto header as a string.
    """
    # Load the type database.
    typedb = utils.get_type_db()
    # Figure out type dependencies in this .proto.
    types = types_pb2.Types()
    text_format.Merge(
        traverse.traverse_file(file_proto, type_whisperer.TypeWhispererVisitor()), types)
    type_dependencies = sum([list(t.type_dependencies) for t in types.types.values()], [])
    for service in file_proto.service:
        for m in service.method:
            type_dependencies.extend([m.input_type[1:], m.output_type[1:]])
    # Determine the envoy/ import paths from type deps.
    envoy_proto_paths = set(
        typedb.types[t].proto_path
        for t in type_dependencies
        if t.startswith('envoy.') and typedb.types[t].proto_path != file_proto.name)

    def camel_case(s):
        return ''.join(t.capitalize() for t in re.split('[\._]', s))

    package_line = 'package %s;\n' % file_proto.package
    file_block = '\n'.join(['syntax = "proto3";\n', package_line])

    options = descriptor_pb2.FileOptions()

    options.java_outer_classname = camel_case(os.path.basename(file_proto.name))
    for msg in file_proto.message_type:
        if msg.name == options.java_outer_classname:
            # This is a workaround for Java outer class names that would otherwise
            # conflict with types defined within the same proto file, see
            # https://github.com/envoyproxy/envoy/pull/13378.
            # TODO: in next major version, make this consistent.
            options.java_outer_classname += "OuterClass"

    options.java_multiple_files = True
    options.java_package = 'io.envoyproxy.' + file_proto.package

    # Workaround packages in generated go code conflicting by transforming:
    # foo/bar/v2 to use barv2 as the package in the generated code
    golang_package_name = ""
    if file_proto.package.split(".")[-1] in ("v2", "v3"):
        name = "".join(file_proto.package.split(".")[-2:])
        golang_package_name = ";" + name
    options.go_package = "".join([
        "github.com/envoyproxy/go-control-plane/",
        file_proto.package.replace(".", "/"), golang_package_name
    ])

    # This is a workaround for C#/Ruby namespace conflicts between packages and
    # objects, see https://github.com/envoyproxy/envoy/pull/3854.
    # TODO(htuch): remove once v3 fixes this naming issue in
    # https://github.com/envoyproxy/envoy/issues/8120.
    if file_proto.package in ['envoy.api.v2.listener', 'envoy.api.v2.cluster']:
        names = [s.capitalize() for s in file_proto.package.split('.')]
        options.csharp_namespace = '.'.join(names) + 'NS'
        options.ruby_package = '::'.join(names) + 'NS'

    if file_proto.service:
        options.java_generic_services = True

    if file_proto.options.HasExtension(migrate_pb2.file_migrate):
        options.Extensions[migrate_pb2.file_migrate].CopyFrom(
            file_proto.options.Extensions[migrate_pb2.file_migrate])

    if file_proto.options.HasExtension(xds_status_pb2.file_status):
        options.Extensions[xds_status_pb2.file_status].CopyFrom(
            file_proto.options.Extensions[xds_status_pb2.file_status])

    if file_proto.options.HasExtension(
            status_pb2.file_status) and file_proto.package.endswith('alpha'):
        options.Extensions[status_pb2.file_status].CopyFrom(
            file_proto.options.Extensions[status_pb2.file_status])

    frozen_proto = file_proto.options.HasExtension(
        status_pb2.file_status) and file_proto.options.Extensions[
            status_pb2.file_status].package_version_status == status_pb2.FROZEN

    if not empty_file:
        options.Extensions[
            status_pb2.file_status].package_version_status = file_proto.options.Extensions[
                status_pb2.file_status].package_version_status

    options_block = format_options(options)

    requires_versioning_import = any(
        protoxform_options.get_versioning_annotation(m.options) for m in file_proto.message_type)

    envoy_imports = list(envoy_proto_paths)
    google_imports = []
    infra_imports = []
    misc_imports = []
    public_imports = []

    for idx, d in enumerate(file_proto.dependency):
        if idx in file_proto.public_dependency:
            public_imports.append(d)
            continue
        elif d.startswith('envoy/annotations') or d.startswith('udpa/annotations'):
            if d == 'envoy/annotations/deprecation.proto' and not frozen_proto:
                # Skip adding, as deprecation proto should be added if
                # import_deprecation_proto is True or the proto is frozen.
                continue
            infra_imports.append(d)
        elif d.startswith('envoy/') or d.startswith('contrib/'):
            # We ignore existing envoy/ imports, since these are computed explicitly
            # from type_dependencies.
            pass
        elif d.startswith('google/'):
            google_imports.append(d)
        elif d.startswith('validate/'):
            infra_imports.append(d)
        elif d in ['udpa/annotations/versioning.proto', 'udpa/annotations/status.proto']:
            # Skip, we decide to add this based on requires_versioning_import and options.
            pass
        else:
            misc_imports.append(d)

    if requires_deprecation_annotation:
        infra_imports.append('envoy/annotations/deprecation.proto')

    if options.HasExtension(status_pb2.file_status):
        infra_imports.append('udpa/annotations/status.proto')

    if requires_versioning_import:
        infra_imports.append('udpa/annotations/versioning.proto')

    def format_import_block(xs):
        if not xs:
            return ''
        return format_block('\n'.join(sorted('import "%s";' % x for x in set(xs) if x)))

    def format_public_import_block(xs):
        if not xs:
            return ''
        return format_block('\n'.join(sorted('import public "%s";' % x for x in xs)))

    import_block = '\n'.join(
        map(format_import_block, [envoy_imports, google_imports, misc_imports, infra_imports]))
    import_block += '\n' + format_public_import_block(public_imports)
    comment_block = format_comments(source_code_info.file_level_comments)

    return ''.join(map(format_block, [file_block, import_block, options_block, comment_block]))


def normalize_field_type_name(type_context, field_fqn):
    """Normalize a fully qualified field type name, e.g.

    .envoy.foo.bar is normalized to foo.bar.

    Considers type context to minimize type prefix.

    Args:
        field_fqn: a fully qualified type name from FieldDescriptorProto.type_name.
        type_context: contextual information for message/enum/field.

    Returns:
        Normalized type name as a string.
    """
    if field_fqn.startswith('.'):
        # If there's a [#allow-fully-qualified-name] annotation, return the FQN
        # field type without attempting to normalize.
        if annotations.ALLOW_FULLY_QUALIFIED_NAME_ANNOTATION in type_context.leading_comment.annotations:
            return field_fqn

        # Let's say we have type context namespace a.b.c.d.e and the type we're
        # trying to normalize is a.b.d.e. We take (from the end) on package fragment
        # at a time, and apply the inner-most evaluation that protoc performs to see
        # if we evaluate to the fully qualified type. If so, we're done. It's not
        # sufficient to compute common prefix and drop that, since in the above
        # example the normalized type name would be d.e, which proto resolves inner
        # most as a.b.c.d.e (bad) instead of the intended a.b.d.e.
        field_fqn_splits = field_fqn[1:].split('.')
        type_context_splits = type_context.name.split('.')[:-1]
        remaining_field_fqn_splits = deque(field_fqn_splits[:-1])
        normalized_splits = deque([field_fqn_splits[-1]])

        if list(remaining_field_fqn_splits)[:1] != type_context_splits[:1] and (
                len(remaining_field_fqn_splits) == 0
                or remaining_field_fqn_splits[0] in type_context_splits[1:]):
            # Notice that in some cases it is error-prone to normalize a type name.
            # E.g., it would be an error to replace ".external.Type" with "external.Type"
            # in the context of "envoy.extensions.type.external.vX.Config".
            # In such a context protoc resolves "external.Type" into
            # "envoy.extensions.type.external.Type", which is exactly what the use of a
            # fully-qualified name ".external.Type" was meant to prevent.
            #
            # A type SHOULD remain fully-qualified under the following conditions:
            # 1. its root package is different from the root package of the context type
            # 2. EITHER the type doesn't belong to any package at all
            #    OR     its root package has a name that collides with one of the packages
            #           of the context type
            #
            # E.g.,
            # a) although ".some.Type" has a different root package than the context type
            #    "TopLevelType", it is still safe to normalize it into "some.Type"
            # b) although ".google.protobuf.Any" has a different root package than the context type
            #    "envoy.api.v2.Cluster", it still safe to normalize it into "google.protobuf.Any"
            # c) it is error-prone to normalize ".TopLevelType" in the context of "some.Type"
            #    into "TopLevelType"
            # d) it is error-prone to normalize ".external.Type" in the context of
            #    "envoy.extensions.type.external.vX.Config" into "external.Type"
            return field_fqn

        def equivalent_in_type_context(splits):
            type_context_splits_tmp = deque(type_context_splits)
            while type_context_splits_tmp:
                # If we're in a.b.c and the FQN is a.d.Foo, we want to return true once
                # we have type_context_splits_tmp as [a] and splits as [d, Foo].
                if list(type_context_splits_tmp) + list(splits) == field_fqn_splits:
                    return True
                # If we're in a.b.c.d.e.f and the FQN is a.b.d.e.Foo, we want to return True
                # once we have type_context_splits_tmp as [a] and splits as [b, d, e, Foo], but
                # not when type_context_splits_tmp is [a, b, c] and FQN is [d, e, Foo].
                if len(splits) > 1 and '.'.join(type_context_splits_tmp).endswith('.'.join(
                        list(splits)[:-1])):
                    return False
                type_context_splits_tmp.pop()
            return False

        while remaining_field_fqn_splits and not equivalent_in_type_context(normalized_splits):
            normalized_splits.appendleft(remaining_field_fqn_splits.pop())

        # `extensions` is a keyword in proto2, and protoc will throw error if a type name
        # starts with `extensions.`.
        if normalized_splits[0] == 'extensions':
            normalized_splits.appendleft(remaining_field_fqn_splits.pop())

        return '.'.join(normalized_splits)
    return field_fqn


def type_name_from_fqn(fqn):
    return fqn[1:]


def format_field_type(type_context, field):
    """Format a FieldDescriptorProto type description.

    Args:
        type_context: contextual information for message/enum/field.
        field: FieldDescriptor proto.

    Returns:
        Formatted proto field type as string.
    """
    label = 'repeated ' if field.label == field.LABEL_REPEATED else ''
    type_name = label + normalize_field_type_name(type_context, field.type_name)

    if field.type == field.TYPE_MESSAGE:
        if type_context.map_typenames and type_name_from_fqn(
                field.type_name) in type_context.map_typenames:
            return 'map<%s, %s>' % tuple(
                map(
                    functools.partial(format_field_type, type_context),
                    type_context.map_typenames[type_name_from_fqn(field.type_name)]))
        return type_name
    elif field.type_name:
        return type_name

    if field.type in constants.FIELD_TYPE_NAMES:
        return label + constants.FIELD_TYPE_NAMES[field.type]
    raise ProtoPrintError('Unknown field type ' + str(field.type))


def format_service_method(type_context, method):
    """Format a service MethodDescriptorProto.

    Args:
        type_context: contextual information for method.
        method: MethodDescriptorProto proto.

    Returns:
        Formatted service method as string.
    """

    def format_streaming(s):
        return 'stream ' if s else ''

    leading_comment, trailing_comment = format_type_context_comments(type_context)
    return '%srpc %s(%s%s%s) returns (%s%s) {%s}\n' % (
        leading_comment, method.name, trailing_comment, format_streaming(
            method.client_streaming), normalize_field_type_name(
                type_context, method.input_type), format_streaming(method.server_streaming),
        normalize_field_type_name(type_context, method.output_type), format_options(method.options))


def format_field(type_context, field):
    """Format FieldDescriptorProto as a proto field.

    Args:
        type_context: contextual information for message/enum/field.
        field: FieldDescriptor proto.

    Returns:
        Formatted proto field as a string.
    """
    if protoxform_options.has_hide_option(field.options):
        return ''
    leading_comment, trailing_comment = format_type_context_comments(type_context)

    return '%s%s %s = %d%s;\n%s' % (
        leading_comment, format_field_type(type_context, field), field.name, field.number,
        format_options(field.options), trailing_comment)


def format_enum_value(type_context, value):
    """Format a EnumValueDescriptorProto as a proto enum value.

    Args:
        type_context: contextual information for message/enum/field.
        value: EnumValueDescriptorProto.

    Returns:
        Formatted proto enum value as a string.
    """
    if protoxform_options.has_hide_option(value.options):
        return ''
    leading_comment, trailing_comment = format_type_context_comments(type_context)
    formatted_annotations = format_options(value.options)
    return '%s%s = %d%s;\n%s' % (
        leading_comment, value.name, value.number, formatted_annotations, trailing_comment)


def text_format_value(field, value):
    """Format the value as protobuf text format

    Args:
        field: a FieldDescriptor that describes the field
        value: the value stored in the field

    Returns:
        value in protobuf text format
    """
    out = io.StringIO()
    text_format.PrintFieldValue(field, value, out)
    return out.getvalue()


def format_options(options):
    """Format *Options (e.g.

    MessageOptions, FieldOptions) message.

    Args:
        options: A *Options (e.g. MessageOptions, FieldOptions) message.

    Returns:
        Formatted options as a string.
    """

    formatted_options = []
    for option_descriptor, option_value in sorted(options.ListFields(), key=lambda x: x[0].number):
        option_name = '({})'.format(
            option_descriptor.full_name
        ) if option_descriptor.is_extension else option_descriptor.name
        if option_descriptor.message_type and option_descriptor.label != option_descriptor.LABEL_REPEATED:
            formatted_options.extend([
                '{}.{} = {}'.format(option_name, subfield.name, text_format_value(subfield, value))
                for subfield, value in option_value.ListFields()
            ])
        else:
            formatted_options.append(
                '{} = {}'.format(option_name, text_format_value(option_descriptor, option_value)))

    if formatted_options:
        if options.DESCRIPTOR.name in ('EnumValueOptions', 'FieldOptions'):
            return '[{}]'.format(','.join(formatted_options))
        else:
            return format_block(
                ''.join(
                    'option {};\n'.format(formatted_option)
                    for formatted_option in formatted_options))
    return ''


def format_reserved(enum_or_msg_proto):
    """Format reserved values/names in a [Enum]DescriptorProto.

    Args:
        enum_or_msg_proto: [Enum]DescriptorProto message.

    Returns:
        Formatted enum_or_msg_proto as a string.
    """
    rrs = copy.deepcopy(enum_or_msg_proto.reserved_range)
    # Fixups for singletons that don't seem to always have [inclusive, exclusive)
    # format when parsed by protoc.
    for rr in rrs:
        if rr.start == rr.end:
            rr.end += 1
    reserved_fields = format_block(
        'reserved %s;\n'
        % ','.join(map(str, sum([list(range(rr.start, rr.end)) for rr in rrs], [])))) if rrs else ''
    if enum_or_msg_proto.reserved_name:
        reserved_fields += format_block(
            'reserved %s;\n' % ', '.join('"%s"' % n for n in enum_or_msg_proto.reserved_name))
    return reserved_fields


class ProtoFormatVisitor(visitor.Visitor):
    """Visitor to generate a proto representation from a FileDescriptor proto.

    See visitor.Visitor for visitor method docs comments.
    """
    _api_version = None
    _requires_deprecation_annotation_import = False

    def __init__(self, params):
        if params['type_db_path']:
            utils.load_type_db(params['type_db_path'])

        self.clang_format_config = pathlib.Path(params[".clang-format"])
        if not self.clang_format_config.exists():
            raise ProtoPrintError(f"Unable to find .clang-format file: {self.clang_format_config}")

        if extra_args := params.get("extra_args"):
            if extra_args.startswith("api_version:"):
                self._api_version = extra_args.split(":")[1]

    @cached_property
    def current_api_version(self):
        return version.Version(self._api_version or envoy_repo.API_VERSION)

    @cached_property
    def _deprecated_annotation_version_value(self):
        return f"{self.current_api_version.major}.{self.current_api_version.minor}"

    def _add_deprecation_version(self, field_or_evalue, deprecation_tag, disallowed_tag):
        """Adds a deprecation version annotation if needed to the given field or enum value.
        The annotation is added if all the following hold:
        - The field or enum value are marked as deprecated.
        - The proto is not frozen.
        - The field or enum value are not marked as hidden.
        - The field or enum value do not already have a version annotation.
        - The field or enum value name is not ENVOY_DEPRECATED_UNAVIALABLE_NAME.
        If a field or enum value are marked with an annotation, the
        _requires_deprecation_annotation_import flag is set.
        The function also validates that if a field or enum value already have the deprecated
        annotation value, then this value is a valid one ("X.Y" where X and Y are valid major,
        and minor versions, respectively).
        """
        if field_or_evalue.options.deprecated and not self._frozen_proto and \
                not protoxform_options.has_hide_option(field_or_evalue.options):
            # If the field or enum value has annotation from deprecation.proto, need to import it.
            self._requires_deprecation_annotation_import = (
                self._requires_deprecation_annotation_import
                or field_or_evalue.options.HasExtension(deprecation_tag)
                or field_or_evalue.options.HasExtension(disallowed_tag))
            if field_or_evalue.name != ENVOY_DEPRECATED_UNAVIALABLE_NAME:
                # If there's a deprecated version annotation, ensure it is valid.
                if field_or_evalue.options.HasExtension(deprecation_tag):
                    if not api_version_utils.is_deprecated_annotation_version(
                            field_or_evalue.options.Extensions[deprecation_tag]):
                        raise ProtoPrintError(
                            'Error while parsing "deprecated_at_minor_version_enum" annotation "%s" value for enum value %s.'
                            % (
                                field_or_evalue.options.Extensions[deprecation_tag],
                                field_or_evalue.name))
                else:
                    # Add the current version as a deprecated version annotation.
                    self._requires_deprecation_annotation_import = True
                    field_or_evalue.options.Extensions[
                        deprecation_tag] = self._deprecated_annotation_version_value

    def visit_service(self, service_proto, type_context):
        leading_comment, trailing_comment = format_type_context_comments(type_context)
        methods = '\n'.join(
            format_service_method(type_context.extend_method(index, m.name), m)
            for index, m in enumerate(service_proto.method))
        options = format_block(format_options(service_proto.options))
        return '%sservice %s {\n%s%s%s\n}\n' % (
            leading_comment, service_proto.name, options, trailing_comment, methods)

    def visit_enum(self, enum_proto, type_context):
        if protoxform_options.has_hide_option(enum_proto.options):
            return ''
        # Verify that not hidden deprecated enum values of non-frozen protos have valid version
        # annotations.

        for v in enum_proto.value:
            self._add_deprecation_version(
                v, deprecation_pb2.deprecated_at_minor_version_enum,
                deprecation_pb2.disallowed_by_default_enum)
        leading_comment, trailing_comment = format_type_context_comments(type_context)
        formatted_options = format_options(enum_proto.options)
        reserved_fields = format_reserved(enum_proto)
        values = [
            format_enum_value(type_context.extend_field(index, value.name), value)
            for index, value in enumerate(enum_proto.value)
        ]
        joined_values = ('\n' if any('//' in v for v in values) else '').join(values)
        return '%senum %s {\n%s%s%s%s\n}\n' % (
            leading_comment, enum_proto.name, trailing_comment, formatted_options, reserved_fields,
            joined_values)

    def visit_message(self, msg_proto, type_context, nested_msgs, nested_enums):
        # Skip messages synthesized to represent map types.
        if msg_proto.options.map_entry:
            return ''
        if protoxform_options.has_hide_option(msg_proto.options):
            return ''
        annotation_xforms = {
            annotations.NEXT_FREE_FIELD_ANNOTATION: create_next_free_field_xform(msg_proto)
        }
        leading_comment, trailing_comment = format_type_context_comments(
            type_context, annotation_xforms)
        formatted_options = format_options(msg_proto.options)
        formatted_enums = format_block('\n'.join(nested_enums))
        formatted_msgs = format_block('\n'.join(nested_msgs))
        reserved_fields = format_reserved(msg_proto)
        # Recover the oneof structure. This needs some extra work, since
        # DescriptorProto just gives use fields and a oneof_index that can allow
        # recovery of the original oneof placement.
        fields = ''
        oneof_index = None
        for index, field in enumerate(msg_proto.field):
            if oneof_index is not None:
                if not field.HasField('oneof_index') or field.oneof_index != oneof_index:
                    fields += '}\n\n'
                    oneof_index = None
            if oneof_index is None and field.HasField('oneof_index'):
                oneof_index = field.oneof_index
                assert (oneof_index < len(msg_proto.oneof_decl))
                oneof_proto = msg_proto.oneof_decl[oneof_index]
                oneof_leading_comment, oneof_trailing_comment = format_type_context_comments(
                    type_context.extend_oneof(oneof_index, field.name))
                fields += '%soneof %s {\n%s%s' % (
                    oneof_leading_comment, oneof_proto.name, oneof_trailing_comment,
                    format_options(oneof_proto.options))
            # Verify that deprecated fields (that are not hidden and are not of frozen protos)
            # have a minor version annotation.
            self._add_deprecation_version(
                field, deprecation_pb2.deprecated_at_minor_version,
                deprecation_pb2.disallowed_by_default)
            fields += format_block(
                format_field(type_context.extend_field(index, field.name), field))
        if oneof_index is not None:
            fields += '}\n\n'
        return '%smessage %s {\n%s%s%s%s%s%s\n}\n' % (
            leading_comment, msg_proto.name, trailing_comment, formatted_options, formatted_enums,
            formatted_msgs, reserved_fields, fields)

    def visit_file(self, file_proto, type_context, services, msgs, enums):
        empty_file = len(services) == 0 and len(enums) == 0 and len(msgs) == 0
        header = format_header_from_file(
            type_context.source_code_info, file_proto, empty_file,
            self._requires_deprecation_annotation_import)
        formatted_services = format_block('\n'.join(services))
        formatted_enums = format_block('\n'.join(enums))
        formatted_msgs = format_block('\n'.join(msgs))
        return clang_format(
            extract_clang_proto_style(self.clang_format_config.read_text()),
            header + formatted_services + formatted_enums + formatted_msgs)


class ProtoprintTraverser:

    def traverse_file(self, file_proto, visitor):
        # TODO(phlax): Figure out how to pass this to visitors
        #   This currently breaks the Visitor contract, ie class props should not be set on individual visitation.
        visitor._frozen_proto = (
            file_proto.options.Extensions[status_pb2.file_status].package_version_status ==
            status_pb2.FROZEN)
        return traverse.traverse_file(file_proto, visitor)


def main(data=None):
    utils.load_protos()

    plugin.plugin([plugin.direct_output_descriptor('.proto', ProtoFormatVisitor, want_params=True)],
                  traverser=ProtoprintTraverser().traverse_file)


if __name__ == '__main__':
    main()
