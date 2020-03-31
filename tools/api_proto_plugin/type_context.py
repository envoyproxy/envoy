"""Type context for FileDescriptorProto traversal."""

from collections import namedtuple

from tools.api_proto_plugin import annotations


class Comment(object):
  """Wrapper for proto source comments."""

  def __init__(self, comment, file_level_annotations=None):
    self.raw = comment
    self.file_level_annotations = file_level_annotations
    self.annotations = annotations.ExtractAnnotations(self.raw, file_level_annotations)

  def getCommentWithTransforms(self, annotation_xforms):
    """Return transformed comment with annotation transformers.

    Args:
      annotation_xforms: a dict of transformers for annotations in leading comment.

    Returns:
      transformed Comment object.
    """
    return Comment(annotations.XformAnnotation(self.raw, annotation_xforms),
                   self.file_level_annotations)


class SourceCodeInfo(object):
  """Wrapper for SourceCodeInfo proto."""

  def __init__(self, name, source_code_info):
    self.name = name
    self.proto = source_code_info
    # Map from path to SourceCodeInfo.Location
    self._locations = {str(location.path): location for location in self.proto.location}
    self._file_level_comments = None
    self._file_level_annotations = None

  @property
  def file_level_comments(self):
    """Obtain inferred file level comment."""
    if self._file_level_comments:
      return self._file_level_comments
    comments = []
    # We find the earliest detached comment by first finding the maximum start
    # line for any location and then scanning for any earlier locations with
    # detached comments.
    earliest_detached_comment = max(location.span[0] for location in self.proto.location) + 1
    for location in self.proto.location:
      if location.leading_detached_comments and location.span[0] < earliest_detached_comment:
        comments = location.leading_detached_comments
        earliest_detached_comment = location.span[0]
    self._file_level_comments = comments
    return comments

  @property
  def file_level_annotations(self):
    """Obtain inferred file level annotations."""
    if self._file_level_annotations:
      return self._file_level_annotations
    self._file_level_annotations = dict(
        sum([list(annotations.ExtractAnnotations(c).items()) for c in self.file_level_comments],
            []))
    return self._file_level_annotations

  def LocationPathLookup(self, path):
    """Lookup SourceCodeInfo.Location by path in SourceCodeInfo.

    Args:
      path: a list of path indexes as per
        https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.

    Returns:
      SourceCodeInfo.Location object if found, otherwise None.
    """
    return self._locations.get(str(path), None)

  # TODO(htuch): consider integrating comment lookup with overall
  # FileDescriptorProto, perhaps via two passes.
  def LeadingCommentPathLookup(self, path):
    """Lookup leading comment by path in SourceCodeInfo.

    Args:
      path: a list of path indexes as per
        https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.

    Returns:
      Comment object.
    """
    location = self.LocationPathLookup(path)
    if location is not None:
      return Comment(location.leading_comments, self.file_level_annotations)
    return Comment('')

  def LeadingDetachedCommentsPathLookup(self, path):
    """Lookup leading detached comments by path in SourceCodeInfo.

    Args:
      path: a list of path indexes as per
        https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.

    Returns:
      List of detached comment strings.
    """
    location = self.LocationPathLookup(path)
    if location is not None and location.leading_detached_comments != self.file_level_comments:
      return location.leading_detached_comments
    return []

  def TrailingCommentPathLookup(self, path):
    """Lookup trailing comment by path in SourceCodeInfo.

    Args:
      path: a list of path indexes as per
        https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.

    Returns:
      Raw detached comment string
    """
    location = self.LocationPathLookup(path)
    if location is not None:
      return location.trailing_comments
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
    # proto synthesis and is dynamically recovered in TraverseMessage.
    self.map_typenames = {}
    # Map from a message's oneof index to the fields sharing a oneof.
    self.oneof_fields = {}
    # Map from a message's oneof index to the name of oneof.
    self.oneof_names = {}
    # Map from a message's oneof index to the "required" bool property.
    self.oneof_required = {}
    self.type_name = 'file'
    self.deprecated = False

  def _Extend(self, path, type_name, name, deprecated=False):
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
    extended.deprecated = self.deprecated or deprecated
    return extended

  def ExtendMessage(self, index, name, deprecated):
    """Extend type context with a message.

    Args:
      index: message index in file.
      name: message name.
      deprecated: is the message depreacted?
    """
    return self._Extend([4, index], 'message', name, deprecated)

  def ExtendNestedMessage(self, index, name, deprecated):
    """Extend type context with a nested message.

    Args:
      index: nested message index in message.
      name: message name.
      deprecated: is the message depreacted?
    """
    return self._Extend([3, index], 'message', name, deprecated)

  def ExtendField(self, index, name):
    """Extend type context with a field.

    Args:
      index: field index in message.
      name: field name.
    """
    return self._Extend([2, index], 'field', name)

  def ExtendEnum(self, index, name, deprecated):
    """Extend type context with an enum.

    Args:
      index: enum index in file.
      name: enum name.
      deprecated: is the message depreacted?
    """
    return self._Extend([5, index], 'enum', name, deprecated)

  def ExtendService(self, index, name):
    """Extend type context with a service.

    Args:
      index: service index in file.
      name: service name.
    """
    return self._Extend([6, index], 'service', name)

  def ExtendNestedEnum(self, index, name, deprecated):
    """Extend type context with a nested enum.

    Args:
      index: enum index in message.
      name: enum name.
      deprecated: is the message depreacted?
    """
    return self._Extend([4, index], 'enum', name, deprecated)

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
    return self._Extend([8, index], 'oneof', name)

  def ExtendMethod(self, index, name):
    """Extend type context with a service method declaration.

    Args:
      index: method index in service.
      name: method name.
    """
    return self._Extend([2, index], 'method', name)

  @property
  def location(self):
    """SourceCodeInfo.Location for type context."""
    return self.source_code_info.LocationPathLookup(self.path)

  @property
  def leading_comment(self):
    """Leading comment for type context."""
    return self.source_code_info.LeadingCommentPathLookup(self.path)

  @property
  def leading_detached_comments(self):
    """Leading detached comments for type context."""
    return self.source_code_info.LeadingDetachedCommentsPathLookup(self.path)

  @property
  def trailing_comment(self):
    """Trailing comment for type context."""
    return self.source_code_info.TrailingCommentPathLookup(self.path)
