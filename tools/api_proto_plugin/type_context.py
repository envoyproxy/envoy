"""Type context for FileDescriptorProto traversal."""

from functools import cached_property

from tools.api_proto_plugin import annotations


class Comment(object):
    """Wrapper for proto source comments."""

    def __init__(self, comment, file_level_annotations=None):
        self.raw = comment
        self.file_level_annotations = file_level_annotations
        self.annotations = annotations.extract_annotations(self.raw, file_level_annotations)

    def get_comment_with_transforms(self, annotation_xforms):
        """Return transformed comment with annotation transformers.

        Args:
            annotation_xforms: a dict of transformers for annotations in leading comment.

        Returns:
            transformed Comment object.
        """
        return Comment(
            annotations.xform_annotation(self.raw, annotation_xforms), self.file_level_annotations)


class SourceCodeInfo(object):
    """Wrapper for SourceCodeInfo proto."""

    def __init__(self, name, source_code_info):
        self.name = name
        self.proto = source_code_info
        self._file_level_annotations = None

    @cached_property
    def locations(self):
        # Map from path to SourceCodeInfo.Location
        return {str(location.path): location for location in self.proto.location}

    @cached_property
    def file_level_comments(self):
        """Obtain inferred file level comment."""
        comments = []
        # We find the earliest detached comment by first finding the maximum start
        # line for any location and then scanning for any earlier locations with
        # detached comments.
        earliest_detached_comment = max(location.span[0] for location in self.proto.location) + 1
        for location in self.proto.location:
            if location.leading_detached_comments and location.span[0] < earliest_detached_comment:
                comments = [
                    self._strip_comment(comment) for comment in location.leading_detached_comments
                ]
                earliest_detached_comment = location.span[0]
        return comments

    @cached_property
    def file_level_annotations(self):
        """Obtain inferred file level annotations."""
        return dict(
            sum([
                list(annotations.extract_annotations(c).items()) for c in self.file_level_comments
            ], []))

    def location_path_lookup(self, path):
        """Lookup SourceCodeInfo.Location by path in SourceCodeInfo.

        Args:
            path: a list of path indexes as per
              https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.

        Returns:
            SourceCodeInfo.Location object if found, otherwise None.
        """
        return self.locations.get(str(path), None)

    # TODO(htuch): consider integrating comment lookup with overall
    # FileDescriptorProto, perhaps via two passes.
    def leading_comment_path_lookup(self, path):
        """Lookup leading comment by path in SourceCodeInfo.

        Args:
            path: a list of path indexes as per
               https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.

        Returns:
            Comment object.
        """
        location = self.location_path_lookup(path)
        if location is not None:
            return Comment(
                self._strip_comment(location.leading_comments), self.file_level_annotations)
        return Comment('')

    def leading_detached_comments_path_lookup(self, path):
        """Lookup leading detached comments by path in SourceCodeInfo.

        Args:
            path: a list of path indexes as per
               https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.

        Returns:
            List of detached comment strings.
        """
        location = self.location_path_lookup(path)
        if location is not None:
            comments = [
                self._strip_comment(comment) for comment in location.leading_detached_comments
            ]
            if comments != self.file_level_comments:
                return comments
        return []

    def trailing_comment_path_lookup(self, path):
        """Lookup trailing comment by path in SourceCodeInfo.

        Args:
            path: a list of path indexes as per
               https://github.com/google/protobuf/blob/a08b03d4c00a5793b88b494f672513f6ad46a681/src/google/protobuf/descriptor.proto#L717.

        Returns:
            Raw detached comment string
        """
        location = self.location_path_lookup(path)
        if location is not None:
            return self._strip_comment(location.trailing_comments)
        return ''

    def _strip_comment(self, comment):
        return "\n".join(l[1:] for l in comment.split("\n"))


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
        # proto synthesis and is dynamically recovered in traverse_message.
        self.map_typenames = {}
        # Map from a message's oneof index to the fields sharing a oneof.
        self.oneof_fields = {}
        # Map from a message's oneof index to the name of oneof.
        self.oneof_names = {}
        # Map from a message's oneof index to the "required" bool property.
        self.oneof_required = {}
        self.type_name = 'file'
        self.deprecated = False

    def _extend(self, path, type_name, name, deprecated=False):
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

    def extend_message(self, index, name, deprecated):
        """Extend type context with a message.

        Args:
            index: message index in file.
            name: message name.
            deprecated: is the message depreacted?
        """
        return self._extend([4, index], 'message', name, deprecated)

    def extend_nested_message(self, index, name, deprecated):
        """Extend type context with a nested message.

        Args:
            index: nested message index in message.
            name: message name.
            deprecated: is the message depreacted?
        """
        return self._extend([3, index], 'message', name, deprecated)

    def extend_field(self, index, name):
        """Extend type context with a field.

        Args:
            index: field index in message.
            name: field name.
        """
        return self._extend([2, index], 'field', name)

    def extend_enum(self, index, name, deprecated):
        """Extend type context with an enum.

        Args:
            index: enum index in file.
            name: enum name.
            deprecated: is the message depreacted?
        """
        return self._extend([5, index], 'enum', name, deprecated)

    def extend_service(self, index, name):
        """Extend type context with a service.

        Args:
            index: service index in file.
            name: service name.
        """
        return self._extend([6, index], 'service', name)

    def extend_nested_enum(self, index, name, deprecated):
        """Extend type context with a nested enum.

    Args:
      index: enum index in message.
      name: enum name.
      deprecated: is the message depreacted?
    """
        return self._extend([4, index], 'enum', name, deprecated)

    def extend_enum_value(self, index, name):
        """Extend type context with an enum enum.

        Args:
            index: enum value index in enum.
            name: value name.
        """
        return self._extend([2, index], 'enum_value', name)

    def extend_oneof(self, index, name):
        """Extend type context with an oneof declaration.

        Args:
            index: oneof index in oneof_decl.
            name: oneof name.
        """
        return self._extend([8, index], 'oneof', name)

    def extend_method(self, index, name):
        """Extend type context with a service method declaration.

        Args:
            index: method index in service.
            name: method name.
        """
        return self._extend([2, index], 'method', name)

    @property
    def location(self):
        """SourceCodeInfo.Location for type context."""
        return self.source_code_info.location_path_lookup(self.path)

    @property
    def leading_comment(self):
        """Leading comment for type context."""
        return self.source_code_info.leading_comment_path_lookup(self.path)

    @property
    def leading_detached_comments(self):
        """Leading detached comments for type context."""
        return self.source_code_info.leading_detached_comments_path_lookup(self.path)

    @property
    def trailing_comment(self):
        """Trailing comment for type context."""
        return self.source_code_info.trailing_comment_path_lookup(self.path)
