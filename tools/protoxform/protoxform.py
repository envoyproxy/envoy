# protoc plugin to map from FileDescriptorProtos to a canonicaly formatted
# proto.
#
# protoxform is currently only a formatting tool, but will act as the basis for
# vN -> v(N+1) API migration tooling, allowing for things like deprecated field
# removal, package renaming, field movement, providing both .proto and .cc code
# generation to support automation of Envoy API version translation.
#
# See https://github.com/google/protobuf/blob/master/src/google/protobuf/descriptor.proto
# for the underlying protos mentioned in this file. See

from collections import deque
import functools
import os
import re
import subprocess

from tools.api_proto_plugin import annotations
from tools.api_proto_plugin import plugin
from tools.api_proto_plugin import traverse
from tools.api_proto_plugin import visitor
from tools.protoxform import migrate
from tools.protoxform import options
from tools.protoxform import utils
from tools.type_whisperer import type_whisperer
from tools.type_whisperer.types_pb2 import Types

from google.api import annotations_pb2
from google.protobuf import text_format
from validate import validate_pb2

CLANG_FORMAT_STYLE = ('{ColumnLimit: 100, SpacesInContainerLiterals: false, '
                      'AllowShortFunctionsOnASingleLine: false}')

NEXT_FREE_FIELD_MIN = 5


class ProtoXformError(Exception):
  """Base error class for the protoxform module."""


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
    annotation_xforms: a dict of transformers for annotations in leading comment.

  Returns:
    Tuple of formatted leading and trailing comment blocks.
  """
  leading_comment = type_context.leading_comment
  if annotation_xforms:
    leading_comment = leading_comment.getCommentWithTransforms(annotation_xforms)
  leading = FormatComments(list(type_context.leading_detached_comments) + [leading_comment.raw])
  trailing = FormatBlock(FormatComments([type_context.trailing_comment]))
  return leading, trailing


def FormatHeaderFromFile(source_code_info, file_proto):
  """Format proto header.

  Args:
    source_code_info: SourceCodeInfo object.
    file_proto: FileDescriptorProto for file.

  Returns:
    Formatted proto header as a string.
  """
  # Load the type database.
  typedb = utils.LoadTypeDb()
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

  options = [
      'option java_outer_classname = "%s";' % CamelCase(os.path.basename(file_proto.name)),
      'option java_multiple_files = true;',
      'option java_package = "io.envoyproxy.%s";' % file_proto.package,
  ]
  # This is a workaround for C#/Ruby namespace conflicts between packages and
  # objects, see https://github.com/envoyproxy/envoy/pull/3854.
  # TODO(htuch): remove once v3 fixes this naming issue in
  # https://github.com/envoyproxy/envoy/issues/8120.
  if file_proto.package in ['envoy.api.v2.listener', 'envoy.api.v2.cluster']:
    qualified_package = '.'.join(s.capitalize() for s in file_proto.package.split('.')) + 'NS'
    options += [
        'option csharp_namespace = "%s";' % qualified_package,
        'option ruby_package = "%s";' % qualified_package,
    ]
  if file_proto.service:
    options += ['option java_generic_services = true;']
  options_block = FormatBlock('\n'.join(options))

  envoy_imports = list(envoy_proto_paths)
  google_imports = []
  infra_imports = []
  misc_imports = []

  for d in file_proto.dependency:
    if d.startswith('envoy/'):
      # We ignore existing envoy/ imports, since these are computed explicitly
      # from type_dependencies.
      pass
    elif d.startswith('google/'):
      google_imports.append(d)
    elif d.startswith('validate/'):
      infra_imports.append(d)
    else:
      misc_imports.append(d)

  def FormatImportBlock(xs):
    if not xs:
      return ''
    return FormatBlock('\n'.join(sorted('import "%s";' % x for x in xs)))

  import_block = '\n'.join(
      map(FormatImportBlock, [envoy_imports, google_imports, misc_imports, infra_imports]))
  comment_block = FormatComments(source_code_info.file_level_comments)

  return ''.join(map(FormatBlock, [file_block, options_block, import_block, comment_block]))


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
  raise ProtoXformError('Unknown field type ' + str(field.type))


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

  def FormatHttpOptions(options):
    if options.HasExtension(annotations_pb2.http):
      http_options = options.Extensions[annotations_pb2.http]
      return 'option (google.api.http) = { %s };' % str(http_options)
    return ''

  leading_comment, trailing_comment = FormatTypeContextComments(type_context)
  return '%srpc %s(%s%s%s) returns (%s%s) {%s}\n' % (
      leading_comment, method.name, trailing_comment, FormatStreaming(
          method.client_streaming), NormalizeFieldTypeName(
              type_context, method.input_type), FormatStreaming(method.server_streaming),
      NormalizeFieldTypeName(type_context, method.output_type), FormatHttpOptions(method.options))


def FormatValidateFieldRules(rules):
  """Format validate_pb2 rules.

  Args:
    rules: validate_pb2 rules proto.

  Returns:
    Formatted validation rules as string, suitable for proto field annotation.
  """
  return ' '.join('.%s = { %s }' %
                  (field.name, text_format.MessageToString(value, as_one_line=True))
                  for field, value in rules.ListFields())


def FormatField(type_context, field):
  """Format FieldDescriptorProto as a proto field.

  Args:
    type_context: contextual information for message/enum/field.
    field: FieldDescriptor proto.

  Returns:
    Formatted proto field as a string.
  """
  if options.HasHideOption(field.options):
    return ''
  leading_comment, trailing_comment = FormatTypeContextComments(type_context)
  annotations = []
  if field.options.HasExtension(validate_pb2.rules):
    rules = field.options.Extensions[validate_pb2.rules]
    annotations.append('(validate.rules) %s' % FormatValidateFieldRules(rules))
  if field.options.deprecated:
    annotations.append('deprecated = true')
  formatted_annotations = '[ %s]' % ','.join(annotations) if annotations else ''
  return '%s%s %s = %d%s;\n%s' % (leading_comment, FormatFieldType(
      type_context, field), field.name, field.number, formatted_annotations, trailing_comment)


def FormatEnumValue(type_context, value):
  """Format a EnumValueDescriptorProto as a proto enum value.

  Args:
    type_context: contextual information for message/enum/field.
    value: EnumValueDescriptorProto.

  Returns:
    Formatted proto enum value as a string.
  """
  if options.HasHideOption(value.options):
    return ''
  leading_comment, trailing_comment = FormatTypeContextComments(type_context)
  annotations = []
  if value.options.deprecated:
    annotations.append('deprecated = true')
  formatted_annotations = '[ %s]' % ','.join(annotations) if annotations else ''
  return '%s%s = %d%s;\n%s' % (leading_comment, value.name, value.number, formatted_annotations,
                               trailing_comment)


def FormatOptions(options):
  """Format MessageOptions/EnumOptions message.

  Args:
    options: A MessageOptions/EnumOptions message.

  Returns:
    Formatted options as a string.
  """
  if options.deprecated:
    return FormatBlock('option deprecated = true;\n')
  return ''


def FormatReserved(enum_or_msg_proto):
  """Format reserved values/names in a [Enum]DescriptorProto.

  Args:
    enum_or_msg_proto: [Enum]DescriptorProto message.

  Returns:
    Formatted enum_or_msg_proto as a string.
  """
  reserved_fields = FormatBlock('reserved %s;\n' % ','.join(
      map(str, sum([list(range(rr.start, rr.end)) for rr in enum_or_msg_proto.reserved_range],
                   [])))) if enum_or_msg_proto.reserved_range else ''
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
    return '%sservice %s {\n%s%s\n}\n' % (leading_comment, service_proto.name, trailing_comment,
                                          methods)

  def VisitEnum(self, enum_proto, type_context):
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
    if options.HasHideOption(msg_proto.options):
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
        oneof_proto = msg_proto.oneof_decl[oneof_index]
        if oneof_proto.options.HasExtension(validate_pb2.required):
          oneof_options = 'option (validate.required) = true;\n\n'
        else:
          oneof_options = ''
        oneof_leading_comment, oneof_trailing_comment = FormatTypeContextComments(
            type_context.ExtendOneof(oneof_index, field.name))
        fields += '%soneof %s {\n%s%s' % (oneof_leading_comment, oneof_proto.name,
                                          oneof_trailing_comment, oneof_options)
      fields += FormatBlock(FormatField(type_context.ExtendField(index, field.name), field))
    if oneof_index is not None:
      fields += '}\n\n'
    return '%smessage %s {\n%s%s%s%s%s%s\n}\n' % (leading_comment, msg_proto.name, trailing_comment,
                                                  formatted_options, formatted_enums,
                                                  formatted_msgs, reserved_fields, fields)

  def VisitFile(self, file_proto, type_context, services, msgs, enums):
    header = FormatHeaderFromFile(type_context.source_code_info, file_proto)
    formatted_services = FormatBlock('\n'.join(services))
    formatted_enums = FormatBlock('\n'.join(enums))
    formatted_msgs = FormatBlock('\n'.join(msgs))
    return ClangFormat(header + formatted_services + formatted_enums + formatted_msgs)


def Main():
  plugin.Plugin([
      plugin.DirectOutputDescriptor('.v2.proto', ProtoFormatVisitor()),
      plugin.OutputDescriptor('.v3alpha.proto', ProtoFormatVisitor(), migrate.V3MigrationXform)
  ])


if __name__ == '__main__':
  Main()
