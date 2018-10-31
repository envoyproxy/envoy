# protoc plugin to map from FileDescriptorProtos to Envoy doc style RST.
# See https://github.com/google/protobuf/blob/master/src/google/protobuf/descriptor.proto
# for the underlying protos mentioned in this file. See
# http://www.sphinx-doc.org/en/stable/rest.html for Sphinx RST syntax.

from collections import defaultdict
import cProfile
import functools
import os
import pstats
import StringIO
import sys
import re

from google.protobuf.compiler import plugin_pb2
from validate import validate_pb2

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

# Key-value annotation regex.
ANNOTATION_REGEX = re.compile('\[#([\w-]+?):(.*?)\]\s?', re.DOTALL)

# Page/section titles with special prefixes in the proto comments
DOC_TITLE_ANNOTATION = 'protodoc-title'

# Not implemented yet annotation on leading comments, leading to insertion of
# warning on field.
NOT_IMPLEMENTED_WARN_ANNOTATION = 'not-implemented-warn'

# Not implemented yet annotation on leading comments, leading to hiding of
# field.
NOT_IMPLEMENTED_HIDE_ANNOTATION = 'not-implemented-hide'

# Comment. Just used for adding text that will not go into the docs at all.
COMMENT_ANNOTATION = 'comment'

# proto compatibility status.
PROTO_STATUS_ANNOTATION = 'proto-status'

# Where v2 differs from v1..
V2_API_DIFF_ANNOTATION = 'v2-api-diff'

VALID_ANNOTATIONS = set([
    DOC_TITLE_ANNOTATION,
    NOT_IMPLEMENTED_WARN_ANNOTATION,
    NOT_IMPLEMENTED_HIDE_ANNOTATION,
    V2_API_DIFF_ANNOTATION,
    COMMENT_ANNOTATION,
    PROTO_STATUS_ANNOTATION,
])

# These can propagate from file scope to message/enum scope (and be overridden).
INHERITED_ANNOTATIONS = set([
    PROTO_STATUS_ANNOTATION,
])

# Template for data plane API URLs.
# TODO(htuch): Add the ability to build a permalink by feeding a hash
# to the tool or inferring from local tree (only really make sense in CI).
DATA_PLANE_API_URL_FMT = 'https://github.com/envoyproxy/envoy/blob/master/api/%s#L%d'


class ProtodocError(Exception):
  """Base error class for the protodoc module."""


def FormatCommentWithAnnotations(s, annotations, type_name):
  if NOT_IMPLEMENTED_WARN_ANNOTATION in annotations:
    s += '\n.. WARNING::\n  Not implemented yet\n'
  if V2_API_DIFF_ANNOTATION in annotations:
    s += '\n.. NOTE::\n  **v2 API difference**: ' + annotations[V2_API_DIFF_ANNOTATION] + '\n'
  if type_name == 'message' or type_name == 'enum':
    if PROTO_STATUS_ANNOTATION in annotations:
      status = annotations[PROTO_STATUS_ANNOTATION]
      if status not in ['frozen', 'draft', 'experimental']:
        raise ProtodocError('Unknown proto status: %s' % status)
      if status == 'draft' or status == 'experimental':
        s += ('\n.. WARNING::\n This %s type has :ref:`%s '
              '<config_overview_v2_status>` status.\n' % (type_name, status))
  return s


def ExtractAnnotations(s, inherited_annotations=None, type_name='file'):
  """Extract annotations from a given comment string.

  Args:
    s: string that may contains annotations.
    inherited_annotations: annotation map from file-level inherited annotations
      (or None) if this is a file-level comment.
  Returns:
    Pair of string with with annotations stripped and annotation map.
  """
  annotations = {
      k: v
      for k, v in (inherited_annotations or {}).items()
      if k in INHERITED_ANNOTATIONS
  }
  # Extract annotations.
  groups = re.findall(ANNOTATION_REGEX, s)
  # Remove annotations.
  without_annotations = re.sub(ANNOTATION_REGEX, '', s)
  for group in groups:
    annotation = group[0]
    if annotation not in VALID_ANNOTATIONS:
      raise ProtodocError('Unknown annotation: %s' % annotation)
    annotations[group[0]] = group[1].lstrip()
  return FormatCommentWithAnnotations(without_annotations, annotations,
                                      type_name), annotations


class SourceCodeInfo(object):
  """Wrapper for SourceCodeInfo proto."""

  def __init__(self, name, source_code_info):
    self._name = name
    self._proto = source_code_info
    self._leading_comments = {str(location.path): location.leading_comments for location in self._proto.location}
    self._file_level_comment = None

  @property
  def file_level_comment(self):
    """Obtain inferred file level comment."""
    if self._file_level_comment:
      return self._file_level_comment
    comment = ''
    earliest_detached_comment = max(
        max(location.span) for location in self._proto.location)
    for location in self._proto.location:
      if location.leading_detached_comments and location.span[0] < earliest_detached_comment:
        comment = StripLeadingSpace(''.join(
            location.leading_detached_comments)) + '\n'
        earliest_detached_comment = location.span[0]
    self._file_level_comment = comment
    return comment

  def LeadingCommentPathLookup(self, path, type_name):
    """Lookup leading comment by path in SourceCodeInfo.

    Args:
      path: a list of path indexes as per
        https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.
      type_name: name of type the comment belongs to.
    Returns:
      Pair of attached leading comment and Annotation objects, where there is a
      leading comment
      otherwise ('', []).
    """
    leading_comment = self._leading_comments.get(str(path), None)
    if leading_comment is not None:
      _, file_annotations = ExtractAnnotations(self.file_level_comment)
      return ExtractAnnotations(
          StripLeadingSpace(leading_comment) + '\n', file_annotations,
          type_name)
    return '', []

  def GithubUrl(self, path):
    """Obtain data plane API Github URL by path from SourceCodeInfo.

    Args:
      path: a list of path indexes as per
        https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.
    Returns:
      A string with a corresponding data plane API GitHub Url.
    """
    for location in self._proto.location:
      if location.path == path:
        return DATA_PLANE_API_URL_FMT % (self._name, location.span[0])
    return ''


class TypeContext(object):
  """Contextual information for a message/field.

  Provides information around namespaces and enclosing types for fields and
  nested messages/enums.
  """

  def __init__(self, source_code_info, name):
    # SourceCodeInfo as per
    # https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto.
    self.source_code_info = source_code_info
    # path: a list of path indexes as per
    #  https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.
    #  Extended as nested objects are traversed.
    self.path = []
    # Message/enum/field name. Extended as nested objects are traversed.
    self.name = name
    # Map from type name to the correct type annotation string, e.g. from
    # ".envoy.api.v2.Foo.Bar" to "map<string, string>". This is lost during
    # proto synthesis and is dynamically recovered in FormatMessage.
    self.map_typenames = {}
    # Map from a message's oneof index to the fields sharing a oneof.
    self.oneof_fields = {}
    # Map from a message's oneof index to the name of oneof.
    self.oneof_names = {}
    # Map from a message's oneof index to the "required" bool property.
    self.oneof_required = {}
    self.type_name = 'file'

  def _Extend(self, path, type_name, name):
    if not self.name:
      extended_name = name
    else:
      extended_name = '%s.%s' % (self.name, name)
    extended = TypeContext(self.source_code_info, extended_name)
    extended.path = self.path + path
    extended.type_name = type_name
    extended.map_typenames = self.map_typenames.copy()
    extended.oneof_fields = self.oneof_fields.copy()
    extended.oneof_names = self.oneof_names.copy()
    extended.oneof_required = self.oneof_required.copy()
    return extended

  def ExtendMessage(self, index, name):
    """Extend type context with a message.

    Args:
      index: message index in file.
      name: message name.
    """
    return self._Extend([4, index], 'message', name)

  def ExtendNestedMessage(self, index, name):
    """Extend type context with a nested message.

    Args:
      index: nested message index in message.
      name: message name.
    """
    return self._Extend([3, index], 'message', name)

  def ExtendField(self, index, name):
    """Extend type context with a field.

    Args:
      index: field index in message.
      name: field name.
    """
    return self._Extend([2, index], 'field', name)

  def ExtendEnum(self, index, name):
    """Extend type context with an enum.

    Args:
      index: enum index in file.
      name: enum name.
    """
    return self._Extend([5, index], 'enum', name)

  def ExtendNestedEnum(self, index, name):
    """Extend type context with a nested enum.

    Args:
      index: enum index in message.
      name: enum name.
    """
    return self._Extend([4, index], 'enum', name)

  def ExtendEnumValue(self, index, name):
    """Extend type context with an enum enum.

    Args:
      index: enum value index in enum.
      name: value name.
    """
    return self._Extend([2, index], 'enum_value', name)

  def ExtendOneof(self, index, name):
    """Extend type context with an oneof declaration.

    Args:
      index: oneof index in oneof_decl.
      name: oneof name.
    """
    return self._Extend([8, index], "oneof", name)

  def LeadingCommentPathLookup(self):
    return self.source_code_info.LeadingCommentPathLookup(
        self.path, self.type_name)

  def GithubUrl(self):
    return self.source_code_info.GithubUrl(self.path)


def MapLines(f, s):
  """Apply a function across each line in a flat string.

  Args:
    f: A string transform function for a line.
    s: A string consisting of potentially multiple lines.
  Returns:
    A flat string with f applied to each line.
  """
  return '\n'.join(f(line) for line in s.split('\n'))


def Indent(spaces, line):
  """Indent a string."""
  return ' ' * spaces + line


def IndentLines(spaces, lines):
  """Indent a list of strings."""
  return map(functools.partial(Indent, spaces), lines)


def FormatInternalLink(text, ref):
  return ':ref:`%s <%s>`' % (text, ref)


def FormatExternalLink(text, ref):
  return '`%s <%s>`_' % (text, ref)


def FormatHeader(style, text):
  """Format RST header.

  Args:
    style: underline style, e.g. '=', '-'.
    text: header text
  Returns:
    RST formatted header.
  """
  return '%s\n%s\n\n' % (text, style * len(text))


def FormatHeaderFromFile(style, file_level_comment, alt):
  """Format RST header based on special file level title

  Args:
    style: underline style, e.g. '=', '-'.
    file_level_comment: detached comment at top of file.
    alt: If the file_level_comment does not contain a user
         specified title, use the alt text as page title.
  Returns:
    RST formatted header, and file level comment without page title strings.
  """
  anchor = FormatAnchor(FileCrossRefLabel(alt))
  stripped_comment, annotations = ExtractAnnotations(file_level_comment)
  if DOC_TITLE_ANNOTATION in annotations:
    return anchor + FormatHeader(
        style, annotations[DOC_TITLE_ANNOTATION]), stripped_comment
  return anchor + FormatHeader(style, alt), stripped_comment


def FormatFieldTypeAsJson(type_context, field):
  """Format FieldDescriptorProto.Type as a pseudo-JSON string.

  Args:
    type_context: contextual information for message/enum/field.
    field: FieldDescriptor proto.
  Return:
    RST formatted pseudo-JSON string representation of field type.
  """
  if NormalizeFQN(field.type_name) in type_context.map_typenames:
    return '"{...}"'
  if field.label == field.LABEL_REPEATED:
    return '[]'
  if field.type == field.TYPE_MESSAGE:
    return '"{...}"'
  return '"..."'


def FormatMessageAsJson(type_context, msg):
  """Format a message definition DescriptorProto as a pseudo-JSON block.

  Args:
    type_context: contextual information for message/enum/field.
    msg: message definition DescriptorProto.
  Return:
    RST formatted pseudo-JSON string representation of message definition.
  """
  lines = []
  for index, field in enumerate(msg.field):
    field_type_context = type_context.ExtendField(index, field.name)
    leading_comment, comment_annotations = field_type_context.LeadingCommentPathLookup(
    )
    if NOT_IMPLEMENTED_HIDE_ANNOTATION in comment_annotations:
      continue
    lines.append('"%s": %s' % (field.name,
                               FormatFieldTypeAsJson(type_context, field)))

  if lines:
    return '.. code-block:: json\n\n  {\n' + ',\n'.join(IndentLines(
        4, lines)) + '\n  }\n\n'
  else:
    return '.. code-block:: json\n\n  {}\n\n'


def NormalizeFQN(fqn):
  """Normalize a fully qualified field type name.

  Strips leading ENVOY_API_NAMESPACE_PREFIX and ENVOY_PREFIX and makes pretty wrapped type names.

  Args:
    fqn: a fully qualified type name from FieldDescriptorProto.type_name.
  Return:
    Normalized type name.
  """
  if fqn.startswith(ENVOY_API_NAMESPACE_PREFIX):
    return fqn[len(ENVOY_API_NAMESPACE_PREFIX):]
  if fqn.startswith(ENVOY_PREFIX):
    return fqn[len(ENVOY_PREFIX):]
  return fqn


def FormatEmph(s):
  """RST format a string for emphasis."""
  return '*%s*' % s


def FormatFieldType(type_context, field):
  """Format a FieldDescriptorProto type description.

  Adds cross-refs for message types.
  TODO(htuch): Add cross-refs for enums as well.

  Args:
    type_context: contextual information for message/enum/field.
    field: FieldDescriptor proto.
  Return:
    RST formatted field type.
  """
  if field.type_name.startswith(ENVOY_API_NAMESPACE_PREFIX) or field.type_name.startswith(ENVOY_PREFIX):
    type_name = NormalizeFQN(field.type_name)
    if field.type == field.TYPE_MESSAGE:
      if type_context.map_typenames and type_name in type_context.map_typenames:
        return type_context.map_typenames[type_name]
      return FormatInternalLink(type_name, MessageCrossRefLabel(type_name))
    if field.type == field.TYPE_ENUM:
      return FormatInternalLink(type_name, EnumCrossRefLabel(type_name))
  elif field.type_name.startswith(WKT_NAMESPACE_PREFIX):
    wkt = field.type_name[len(WKT_NAMESPACE_PREFIX):]
    return FormatExternalLink(
        wkt,
        'https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#%s'
        % wkt.lower())
  elif field.type_name.startswith(RPC_NAMESPACE_PREFIX):
    rpc = field.type_name[len(RPC_NAMESPACE_PREFIX):]
    return FormatExternalLink(
        rpc,
        'https://cloud.google.com/natural-language/docs/reference/rpc/google.rpc#%s'
        % rpc.lower())
  elif field.type_name:
    return field.type_name

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
    return FormatExternalLink(
        pretty_type_names[field.type],
        'https://developers.google.com/protocol-buffers/docs/proto#scalar')
  raise ProtodocError('Unknown field type ' + str(field.type))


def StripLeadingSpace(s):
  """Remove leading space in flat comment strings."""
  return MapLines(lambda s: s[1:], s)


def FileCrossRefLabel(msg_name):
  """File cross reference label."""
  return 'envoy_api_file_%s' % msg_name


def MessageCrossRefLabel(msg_name):
  """Message cross reference label."""
  return 'envoy_api_msg_%s' % msg_name


def EnumCrossRefLabel(enum_name):
  """Enum cross reference label."""
  return 'envoy_api_enum_%s' % enum_name


def FieldCrossRefLabel(field_name):
  """Field cross reference label."""
  return 'envoy_api_field_%s' % field_name


def EnumValueCrossRefLabel(enum_value_name):
  """Enum value cross reference label."""
  return 'envoy_api_enum_value_%s' % enum_value_name


def FormatAnchor(label):
  """Format a label as an Envoy API RST anchor."""
  return '.. _%s:\n\n' % label


def FormatFieldAsDefinitionListItem(outer_type_context, type_context, field):
  """Format a FieldDescriptorProto as RST definition list item.

  Args:
    outer_type_context: contextual information for enclosing message.
    type_context: contextual information for message/enum/field.
    field: FieldDescriptorProto.
  Returns:
    RST formatted definition list item.
  """
  annotations = []

  anchor = FormatAnchor(FieldCrossRefLabel(type_context.name))
  if field.options.HasExtension(validate_pb2.rules):
    rule = field.options.Extensions[validate_pb2.rules]
    if ((rule.HasField('message') and rule.message.required) or
        (rule.HasField('string') and rule.string.min_bytes > 0) or
        (rule.HasField('repeated') and rule.repeated.min_items > 0)):
      annotations = ['*REQUIRED*']
  leading_comment, comment_annotations = type_context.LeadingCommentPathLookup()
  if NOT_IMPLEMENTED_HIDE_ANNOTATION in comment_annotations:
    return ''

  if field.HasField('oneof_index'):
    oneof_context = outer_type_context.ExtendOneof(field.oneof_index,
                                                   type_context.oneof_names[field.oneof_index])
    oneof_comment, oneof_comment_annotations = oneof_context.LeadingCommentPathLookup()
    if NOT_IMPLEMENTED_HIDE_ANNOTATION in oneof_comment_annotations:
      return ''

    # If the oneof only has one field and marked required, mark the field as required.
    if len(type_context.oneof_fields[field.oneof_index]) == 1 and type_context.oneof_required[
        field.oneof_index]:
      annotations = ['*REQUIRED*']

    if len(type_context.oneof_fields[field.oneof_index]) > 1:
      # Fields in oneof shouldn't be marked as required when we have oneof comment below it.
      annotations = []
      oneof_template = '\nPrecisely one of %s must be set.\n' if type_context.oneof_required[
          field.oneof_index] else '\nOnly one of %s may be set.\n'
      oneof_comment += oneof_template % ', '.join(
          FormatInternalLink(
              f, FieldCrossRefLabel(outer_type_context.ExtendField(i, f).name))
          for i, f in type_context.oneof_fields[field.oneof_index])
  else:
    oneof_comment = ''

  comment = '(%s) ' % ', '.join(
      [FormatFieldType(type_context, field)] + annotations) + leading_comment
  return anchor + field.name + '\n' + MapLines(
      functools.partial(Indent, 2), comment + oneof_comment)


def FormatMessageAsDefinitionList(type_context, msg):
  """Format a DescriptorProto as RST definition list.

  Args:
    type_context: contextual information for message/enum/field.
    msg: DescriptorProto.
  Returns:
    RST formatted definition list item.
  """
  type_context.oneof_fields = defaultdict(list)
  type_context.oneof_required = defaultdict(bool)
  type_context.oneof_names = defaultdict(list)
  for index, field in enumerate(msg.field):
    if field.HasField('oneof_index'):
      _, comment_annotations = type_context.ExtendField(
          index, field.name).LeadingCommentPathLookup()
      if NOT_IMPLEMENTED_HIDE_ANNOTATION in comment_annotations:
        continue
      type_context.oneof_fields[field.oneof_index].append((index, field.name))
  for index, oneof_decl in enumerate(msg.oneof_decl):
    if oneof_decl.options.HasExtension(validate_pb2.required):
      type_context.oneof_required[index] = oneof_decl.options.Extensions[
          validate_pb2.required]
    type_context.oneof_names[index] = oneof_decl.name
  return '\n'.join(
      FormatFieldAsDefinitionListItem(
          type_context, type_context.ExtendField(index, field.name), field)
      for index, field in enumerate(msg.field)) + '\n'


def FormatMessage(type_context, msg):
  """Format a DescriptorProto as RST section.

  Args:
    type_context: contextual information for message/enum/field.
    msg: DescriptorProto.
  Returns:
    RST formatted section.
  """
  # Skip messages synthesized to represent map types.
  if msg.options.map_entry:
    return ''
  # We need to do some extra work to recover the map type annotation from the
  # synthesized messages.
  type_context.map_typenames = {
      '%s.%s' % (type_context.name, nested_msg.name): 'map<%s, %s>' % tuple(
          map(
              functools.partial(FormatFieldType, type_context),
              nested_msg.field))
      for nested_msg in msg.nested_type
      if nested_msg.options.map_entry
  }
  nested_msgs = '\n'.join(
      FormatMessage(
          type_context.ExtendNestedMessage(index, nested_msg.name), nested_msg)
      for index, nested_msg in enumerate(msg.nested_type))
  nested_enums = '\n'.join(
      FormatEnum(
          type_context.ExtendNestedEnum(index, nested_enum.name), nested_enum)
      for index, nested_enum in enumerate(msg.enum_type))
  anchor = FormatAnchor(MessageCrossRefLabel(type_context.name))
  header = FormatHeader('-', type_context.name)
  proto_link = FormatExternalLink('[%s proto]' % type_context.name,
                                  type_context.GithubUrl()) + '\n\n'
  leading_comment, annotations = type_context.LeadingCommentPathLookup()
  if NOT_IMPLEMENTED_HIDE_ANNOTATION in annotations:
    return ''
  return anchor + header + proto_link + leading_comment + FormatMessageAsJson(
      type_context, msg) + FormatMessageAsDefinitionList(
          type_context, msg) + nested_msgs + '\n' + nested_enums


def FormatEnumValueAsDefinitionListItem(type_context, enum_value):
  """Format a EnumValueDescriptorProto as RST definition list item.

  Args:
    type_context: contextual information for message/enum/field.
    enum_value: EnumValueDescriptorProto.
  Returns:
    RST formatted definition list item.
  """
  anchor = FormatAnchor(EnumValueCrossRefLabel(type_context.name))
  default_comment = '*(DEFAULT)* ' if enum_value.number == 0 else ''
  leading_comment, annotations = type_context.LeadingCommentPathLookup()
  if NOT_IMPLEMENTED_HIDE_ANNOTATION in annotations:
    return ''
  comment = default_comment + UNICODE_INVISIBLE_SEPARATOR + leading_comment
  return anchor + enum_value.name + '\n' + MapLines(
      functools.partial(Indent, 2), comment)


def FormatEnumAsDefinitionList(type_context, enum):
  """Format a EnumDescriptorProto as RST definition list.

  Args:
    type_context: contextual information for message/enum/field.
    enum: DescriptorProto.
  Returns:
    RST formatted definition list item.
  """
  return '\n'.join(
      FormatEnumValueAsDefinitionListItem(
          type_context.ExtendEnumValue(index, enum_value.name), enum_value)
      for index, enum_value in enumerate(enum.value)) + '\n'


def FormatEnum(type_context, enum):
  """Format an EnumDescriptorProto as RST section.

  Args:
    type_context: contextual information for message/enum/field.
    enum: EnumDescriptorProto.
  Returns:
    RST formatted section.
  """
  anchor = FormatAnchor(EnumCrossRefLabel(type_context.name))
  header = FormatHeader('-', 'Enum %s' % type_context.name)
  proto_link = FormatExternalLink('[%s proto]' % type_context.name,
                                  type_context.GithubUrl()) + '\n\n'
  leading_comment, annotations = type_context.LeadingCommentPathLookup()
  if NOT_IMPLEMENTED_HIDE_ANNOTATION in annotations:
    return ''
  return anchor + header + proto_link + leading_comment + FormatEnumAsDefinitionList(
      type_context, enum)


def FormatProtoAsBlockComment(proto):
  """Format as RST a proto as a block comment.

  Useful in debugging, not usually referenced.
  """
  return '\n\nproto::\n\n' + MapLines(functools.partial(Indent, 2),
                                      str(proto)) + '\n'


def GenerateRst(proto_file):
  """Generate a RST representation from a FileDescriptor proto."""
  source_code_info = SourceCodeInfo(proto_file.name,
                                    proto_file.source_code_info)
  # Find the earliest detached comment, attribute it to file level.
  # Also extract file level titles if any.
  header, comment = FormatHeaderFromFile(
      '=', source_code_info.file_level_comment, proto_file.name)
  package_prefix = NormalizeFQN('.' + proto_file.package + '.')[:-1]
  package_type_context = TypeContext(source_code_info, package_prefix)
  msgs = '\n'.join(
      FormatMessage(package_type_context.ExtendMessage(index, msg.name), msg)
      for index, msg in enumerate(proto_file.message_type))
  enums = '\n'.join(
      FormatEnum(package_type_context.ExtendEnum(index, enum.name), enum)
      for index, enum in enumerate(proto_file.enum_type))
  debug_proto = FormatProtoAsBlockComment(proto_file)
  return header + comment + msgs + enums  # + debug_proto

def Main():
  # http://www.expobrain.net/2015/09/13/create-a-plugin-for-google-protocol-buffer/
  request = plugin_pb2.CodeGeneratorRequest()
  request.ParseFromString(sys.stdin.read())
  response = plugin_pb2.CodeGeneratorResponse()
  cprofile_enabled = os.getenv('CPROFILE_ENABLED')

  for proto_file in request.proto_file:
    f = response.file.add()
    f.name = proto_file.name + '.rst'
    if cprofile_enabled:
      pr = cProfile.Profile()
      pr.enable()
    # We don't actually generate any RST right now, we just string dump the
    # input proto file descriptor into the output file.
    f.content = GenerateRst(proto_file)
    if cprofile_enabled:
      pr.disable()
      stats_stream = StringIO.StringIO()
      ps = pstats.Stats(pr, stream=stats_stream).sort_stats(os.getenv('CPROFILE_SORTBY', 'cumulative'))
      stats_file = response.file.add()
      stats_file.name = proto_file.name + '.rst.profile'
      ps.print_stats()
      stats_file.content = stats_stream.getvalue()
  sys.stdout.write(response.SerializeToString())

if __name__ == '__main__':
  Main()
