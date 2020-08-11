# FileDescriptorProtos pretty-printer tool.
#
# protoprint.py provides the canonical .proto formatting for the Envoy APIs.
#
# See https://github.com/google/protobuf/blob/master/src/google/protobuf/descriptor.proto
# for the underlying protos mentioned in this file.
#
# Usage: protoprint.py <source file path> <type database path>

from collections import deque
import copy
import functools
import io
import os
import pathlib
import re
import subprocess
import sys

from tools.api_proto_plugin import annotations
from tools.api_proto_plugin import plugin
from tools.api_proto_plugin import traverse
from tools.api_proto_plugin import visitor
from tools.protoxform import options as protoxform_options
from tools.protoxform import utils
from tools.type_whisperer import type_whisperer
from tools.type_whisperer.types_pb2 import Types

from google.protobuf import descriptor_pb2
from google.protobuf import text_format

# Note: we have to include those proto definitions to make FormatOptions work,
# this also serves as allowlist of extended options.
from google.api import annotations_pb2 as _
from validate import validate_pb2 as _
from envoy.annotations import deprecation_pb2 as _
from envoy.annotations import resource_pb2
from udpa.annotations import migrate_pb2
from udpa.annotations import security_pb2 as _
from udpa.annotations import sensitive_pb2 as _
from udpa.annotations import status_pb2

NEXT_FREE_FIELD_MIN = 5


class ProtoPrintError(Exception):
  """Base error class for the protoprint module."""


def ExtractClangProtoStyle(clang_format_text):
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


# Ensure we are using the canonical clang-format proto style.
CLANG_FORMAT_STYLE = ExtractClangProtoStyle(pathlib.Path('.clang-format').read_text())


def ClangFormat(contents):
  """Run proto-style oriented clang-format over given string.

  Args:
    contents: a string with proto contents.

  Returns:
    clang-formatted string
  """
  return subprocess.run(
      ['clang-format',
       '--style=%s' % CLANG_FORMAT_STYLE, '--assume-filename=.proto'],
      input=contents.encode('utf-8'),
      stdout=subprocess.PIPE).stdout


def FormatBlock(block):
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


def FormatComments(comments):
  """Format a list of comment blocks from SourceCodeInfo.

  Prefixes // to each line, separates blocks by spaces.

  Args:
    comments: a list of blocks, each block is a list of strings representing
      lines in each block.

  Returns:
    A string reprenting the formatted comment blocks.
  """

  # TODO(htuch): not sure why this is needed, but clang-format does some weird
  # stuff with // comment indents when we have these trailing \
  def FixupTrailingBackslash(s):
    return s[:-1].rstrip() if s.endswith('\\') else s

  comments = '\n\n'.join(
      '\n'.join(['//%s' % FixupTrailingBackslash(line)
                 for line in comment.split('\n')[:-1]])
      for comment in comments)
  return FormatBlock(comments)


def CreateNextFreeFieldXform(msg_proto):
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


def FormatTypeContextComments(type_context, annotation_xforms=None):
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
    leading_comment = leading_comment.getCommentWithTransforms(annotation_xforms)
  leading = FormatComments(list(type_context.leading_detached_comments) + [leading_comment.raw])
  trailing = FormatBlock(FormatComments([type_context.trailing_comment]))
  return leading, trailing


def FormatHeaderFromFile(source_code_info, file_proto, empty_file):
  """Format proto header.

  Args:
    source_code_info: SourceCodeInfo object.
    file_proto: FileDescriptorProto for file.
    empty_file: are there no message/enum/service defs in file?

  Returns:
    Formatted proto header as a string.
  """
  # Load the type database.
  typedb = utils.GetTypeDb()
  # Figure out type dependencies in this .proto.
  types = Types()
  text_format.Merge(traverse.TraverseFile(file_proto, type_whisperer.TypeWhispererVisitor()), types)
  type_dependencies = sum([list(t.type_dependencies) for t in types.types.values()], [])
  for service in file_proto.service:
    for m in service.method:
      type_dependencies.extend([m.input_type[1:], m.output_type[1:]])
  # Determine the envoy/ import paths from type deps.
  envoy_proto_paths = set(
      typedb.types[t].proto_path
      for t in type_dependencies
      if t.startswith('envoy.') and typedb.types[t].proto_path != file_proto.name)

  def CamelCase(s):
    return ''.join(t.capitalize() for t in re.split('[\._]', s))

  package_line = 'package %s;\n' % file_proto.package
  file_block = '\n'.join(['syntax = "proto3";\n', package_line])

  options = descriptor_pb2.FileOptions()
  options.java_outer_classname = CamelCase(os.path.basename(file_proto.name))
  options.java_multiple_files = True
  options.java_package = 'io.envoyproxy.' + file_proto.package

  # This is a workaround for C#/Ruby namespace conflicts between packages and
  # objects, see https://github.com/envoyproxy/envoy/pull/3854.
  # TODO(htuch): remove once v3 fixes this naming issue in
  # https://github.com/envoyproxy/envoy/issues/8120.
  if file_proto.package in ['envoy.api.v2.listener', 'envoy.api.v2.cluster']:
    qualified_package = '.'.join(s.capitalize() for s in file_proto.package.split('.')) + 'NS'
    options.csharp_namespace = qualified_package
    options.ruby_package = qualified_package

  if file_proto.service:
    options.java_generic_services = True

  if file_proto.options.HasExtension(migrate_pb2.file_migrate):
    options.Extensions[migrate_pb2.file_migrate].CopyFrom(
        file_proto.options.Extensions[migrate_pb2.file_migrate])

  if file_proto.options.HasExtension(
      status_pb2.file_status) and file_proto.package.endswith('alpha'):
    options.Extensions[status_pb2.file_status].CopyFrom(
        file_proto.options.Extensions[status_pb2.file_status])

  if not empty_file:
    options.Extensions[
        status_pb2.file_status].package_version_status = file_proto.options.Extensions[
            status_pb2.file_status].package_version_status

  options_block = FormatOptions(options)

  requires_versioning_import = any(
      protoxform_options.GetVersioningAnnotation(m.options) for m in file_proto.message_type)

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
      infra_imports.append(d)
    elif d.startswith('envoy/'):
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

  if options.HasExtension(status_pb2.file_status):
    infra_imports.append('udpa/annotations/status.proto')

  if requires_versioning_import:
    infra_imports.append('udpa/annotations/versioning.proto')

  def FormatImportBlock(xs):
    if not xs:
      return ''
    return FormatBlock('\n'.join(sorted('import "%s";' % x for x in set(xs) if x)))

  def FormatPublicImportBlock(xs):
    if not xs:
      return ''
    return FormatBlock('\n'.join(sorted('import public "%s";' % x for x in xs)))

  import_block = '\n'.join(
      map(FormatImportBlock, [envoy_imports, google_imports, misc_imports, infra_imports]))
  import_block += '\n' + FormatPublicImportBlock(public_imports)
  comment_block = FormatComments(source_code_info.file_level_comments)

  return ''.join(map(FormatBlock, [file_block, import_block, options_block, comment_block]))


def NormalizeFieldTypeName(type_context, field_fqn):
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
        len(remaining_field_fqn_splits) == 0 or
        remaining_field_fqn_splits[0] in type_context_splits[1:]):
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

    def EquivalentInTypeContext(splits):
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

    while remaining_field_fqn_splits and not EquivalentInTypeContext(normalized_splits):
      normalized_splits.appendleft(remaining_field_fqn_splits.pop())

    # `extensions` is a keyword in proto2, and protoc will throw error if a type name
    # starts with `extensions.`.
    if normalized_splits[0] == 'extensions':
      normalized_splits.appendleft(remaining_field_fqn_splits.pop())

    return '.'.join(normalized_splits)
  return field_fqn


def TypeNameFromFQN(fqn):
  return fqn[1:]


def FormatFieldType(type_context, field):
  """Format a FieldDescriptorProto type description.

  Args:
    type_context: contextual information for message/enum/field.
    field: FieldDescriptor proto.

  Returns:
    Formatted proto field type as string.
  """
  label = 'repeated ' if field.label == field.LABEL_REPEATED else ''
  type_name = label + NormalizeFieldTypeName(type_context, field.type_name)

  if field.type == field.TYPE_MESSAGE:
    if type_context.map_typenames and TypeNameFromFQN(
        field.type_name) in type_context.map_typenames:
      return 'map<%s, %s>' % tuple(
          map(functools.partial(FormatFieldType, type_context),
              type_context.map_typenames[TypeNameFromFQN(field.type_name)]))
    return type_name
  elif field.type_name:
    return type_name

  pretty_type_names = {
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
  if field.type in pretty_type_names:
    return label + pretty_type_names[field.type]
  raise ProtoPrintError('Unknown field type ' + str(field.type))


def FormatServiceMethod(type_context, method):
  """Format a service MethodDescriptorProto.

  Args:
    type_context: contextual information for method.
    method: MethodDescriptorProto proto.

  Returns:
    Formatted service method as string.
  """

  def FormatStreaming(s):
    return 'stream ' if s else ''

  leading_comment, trailing_comment = FormatTypeContextComments(type_context)
  return '%srpc %s(%s%s%s) returns (%s%s) {%s}\n' % (
      leading_comment, method.name, trailing_comment, FormatStreaming(
          method.client_streaming), NormalizeFieldTypeName(
              type_context, method.input_type), FormatStreaming(method.server_streaming),
      NormalizeFieldTypeName(type_context, method.output_type), FormatOptions(method.options))


def FormatField(type_context, field):
  """Format FieldDescriptorProto as a proto field.

  Args:
    type_context: contextual information for message/enum/field.
    field: FieldDescriptor proto.

  Returns:
    Formatted proto field as a string.
  """
  if protoxform_options.HasHideOption(field.options):
    return ''
  leading_comment, trailing_comment = FormatTypeContextComments(type_context)

  return '%s%s %s = %d%s;\n%s' % (leading_comment, FormatFieldType(type_context, field), field.name,
                                  field.number, FormatOptions(field.options), trailing_comment)


def FormatEnumValue(type_context, value):
  """Format a EnumValueDescriptorProto as a proto enum value.

  Args:
    type_context: contextual information for message/enum/field.
    value: EnumValueDescriptorProto.

  Returns:
    Formatted proto enum value as a string.
  """
  if protoxform_options.HasHideOption(value.options):
    return ''
  leading_comment, trailing_comment = FormatTypeContextComments(type_context)
  formatted_annotations = FormatOptions(value.options)
  return '%s%s = %d%s;\n%s' % (leading_comment, value.name, value.number, formatted_annotations,
                               trailing_comment)


def TextFormatValue(field, value):
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


def FormatOptions(options):
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
        option_descriptor.full_name) if option_descriptor.is_extension else option_descriptor.name
    if option_descriptor.message_type and option_descriptor.label != option_descriptor.LABEL_REPEATED:
      formatted_options.extend([
          '{}.{} = {}'.format(option_name, subfield.name, TextFormatValue(subfield, value))
          for subfield, value in option_value.ListFields()
      ])
    else:
      formatted_options.append('{} = {}'.format(option_name,
                                                TextFormatValue(option_descriptor, option_value)))

  if formatted_options:
    if options.DESCRIPTOR.name in ('EnumValueOptions', 'FieldOptions'):
      return '[{}]'.format(','.join(formatted_options))
    else:
      return FormatBlock(''.join(
          'option {};\n'.format(formatted_option) for formatted_option in formatted_options))
  return ''


def FormatReserved(enum_or_msg_proto):
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
  reserved_fields = FormatBlock(
      'reserved %s;\n' %
      ','.join(map(str, sum([list(range(rr.start, rr.end)) for rr in rrs], [])))) if rrs else ''
  if enum_or_msg_proto.reserved_name:
    reserved_fields += FormatBlock('reserved %s;\n' %
                                   ', '.join('"%s"' % n for n in enum_or_msg_proto.reserved_name))
  return reserved_fields


class ProtoFormatVisitor(visitor.Visitor):
  """Visitor to generate a proto representation from a FileDescriptor proto.

  See visitor.Visitor for visitor method docs comments.
  """

  def VisitService(self, service_proto, type_context):
    leading_comment, trailing_comment = FormatTypeContextComments(type_context)
    methods = '\n'.join(
        FormatServiceMethod(type_context.ExtendMethod(index, m.name), m)
        for index, m in enumerate(service_proto.method))
    options = FormatBlock(FormatOptions(service_proto.options))
    return '%sservice %s {\n%s%s%s\n}\n' % (leading_comment, service_proto.name, options,
                                            trailing_comment, methods)

  def VisitEnum(self, enum_proto, type_context):
    if protoxform_options.HasHideOption(enum_proto.options):
      return ''
    leading_comment, trailing_comment = FormatTypeContextComments(type_context)
    formatted_options = FormatOptions(enum_proto.options)
    reserved_fields = FormatReserved(enum_proto)
    values = [
        FormatEnumValue(type_context.ExtendField(index, value.name), value)
        for index, value in enumerate(enum_proto.value)
    ]
    joined_values = ('\n' if any('//' in v for v in values) else '').join(values)
    return '%senum %s {\n%s%s%s%s\n}\n' % (leading_comment, enum_proto.name, trailing_comment,
                                           formatted_options, reserved_fields, joined_values)

  def VisitMessage(self, msg_proto, type_context, nested_msgs, nested_enums):
    # Skip messages synthesized to represent map types.
    if msg_proto.options.map_entry:
      return ''
    if protoxform_options.HasHideOption(msg_proto.options):
      return ''
    annotation_xforms = {
        annotations.NEXT_FREE_FIELD_ANNOTATION: CreateNextFreeFieldXform(msg_proto)
    }
    leading_comment, trailing_comment = FormatTypeContextComments(type_context, annotation_xforms)
    formatted_options = FormatOptions(msg_proto.options)
    formatted_enums = FormatBlock('\n'.join(nested_enums))
    formatted_msgs = FormatBlock('\n'.join(nested_msgs))
    reserved_fields = FormatReserved(msg_proto)
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
        oneof_leading_comment, oneof_trailing_comment = FormatTypeContextComments(
            type_context.ExtendOneof(oneof_index, field.name))
        fields += '%soneof %s {\n%s%s' % (oneof_leading_comment, oneof_proto.name,
                                          oneof_trailing_comment, FormatOptions(
                                              oneof_proto.options))
      fields += FormatBlock(FormatField(type_context.ExtendField(index, field.name), field))
    if oneof_index is not None:
      fields += '}\n\n'
    return '%smessage %s {\n%s%s%s%s%s%s\n}\n' % (leading_comment, msg_proto.name, trailing_comment,
                                                  formatted_options, formatted_enums,
                                                  formatted_msgs, reserved_fields, fields)

  def VisitFile(self, file_proto, type_context, services, msgs, enums):
    empty_file = len(services) == 0 and len(enums) == 0 and len(msgs) == 0
    header = FormatHeaderFromFile(type_context.source_code_info, file_proto, empty_file)
    formatted_services = FormatBlock('\n'.join(services))
    formatted_enums = FormatBlock('\n'.join(enums))
    formatted_msgs = FormatBlock('\n'.join(msgs))
    return ClangFormat(header + formatted_services + formatted_enums + formatted_msgs)


if __name__ == '__main__':
  proto_desc_path = sys.argv[1]
  file_proto = descriptor_pb2.FileDescriptorProto()
  input_text = pathlib.Path(proto_desc_path).read_text()
  if not input_text:
    sys.exit(0)
  text_format.Merge(input_text, file_proto)
  dst_path = pathlib.Path(sys.argv[2])
  utils.LoadTypeDb(sys.argv[3])
  dst_path.write_bytes(traverse.TraverseFile(file_proto, ProtoFormatVisitor()))
