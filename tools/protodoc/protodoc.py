# protoc plugin to map from FileDescriptorProtos to Envoy doc style RST.
# See https://github.com/google/protobuf/blob/master/src/google/protobuf/descriptor.proto
# for the underlying protos mentioned in this file. See
# https://www.sphinx-doc.org/en/master/usage/restructuredtext/basics.html for Sphinx RST syntax.

import logging
import sys
from collections import defaultdict
from functools import cached_property, lru_cache
from typing import Dict, Iterable, Iterator, Set, Tuple

from google.protobuf.descriptor_pb2 import FieldDescriptorProto as field_proto
import yaml

from udpa.annotations import security_pb2
from udpa.annotations import status_pb2 as udpa_status_pb2
from validate import validate_pb2
from xds.annotations.v3 import status_pb2 as xds_status_pb2

from envoy.code.check.checker import BackticksCheck

from tools.api_proto_plugin import annotations, constants, plugin, visitor
from tools.protodoc import jinja
from tools.protodoc.data import data

logger = logging.getLogger(__name__)

WIP_WARNING = (
    '.. warning::\n   This API feature is currently work-in-progress. API features marked as '
    'work-in-progress are not considered stable, are not covered by the :ref:`threat model '
    '<arch_overview_threat_model>`, are not supported by the security team, and are subject to '
    'breaking changes. Do not use this feature without understanding each of the previous '
    'points.\n\n')


class ProtodocError(Exception):
    """Base error class for the protodoc module."""


def github_url(text, type_context):
    """Obtain data plane API Github URL by path from a TypeContext.

    Args:
        type_context: type_context.TypeContext for node.

    Returns:
        A string with a corresponding data plane API GitHub Url.
    """
    if type_context.name.startswith(constants.CNCF_PREFIX[1:]):
        return format_external_link(
            text,
            f"https://github.com/cncf/xds/blob/main/{type_context.source_code_info.name}#L{type_context.location.span[0]}"
        )
    return f":repo:`{text} <api/{type_context.source_code_info.name}#L{type_context.location.span[0]}>`"


@lru_cache
def format_internal_link(text, ref):
    return ':ref:`%s <%s>`' % (text, ref)


@lru_cache
def format_external_link(text, ref):
    return '`%s <%s>`_' % (text, ref)


def rst_anchor(label):
    """Format a label as an Envoy API RST anchor."""
    return f".. _{label}:\n\n"


def rst_header(text, style=None):
    """Format RST header.
    """
    style = style or "-"
    return f"{text}\n{style * len(text)}\n\n"


@lru_cache
def normalize_field_type_name(field_fqn):
    """Normalize a fully qualified field type name, e.g.

    .envoy.foo.bar.

    Strips leading constants.ENVOY_API_NAMESPACE_PREFIX and constants.ENVOY_PREFIX.

    Args:
        field_fqn: a fully qualified type name from FieldDescriptorProto.type_name.
    Return: Normalized type name.
    """
    if field_fqn.startswith(constants.ENVOY_API_NAMESPACE_PREFIX):
        return field_fqn[len(constants.ENVOY_API_NAMESPACE_PREFIX):]
    if field_fqn.startswith(constants.ENVOY_PREFIX):
        return field_fqn[len(constants.ENVOY_PREFIX):]
    return field_fqn


def normalize_type_context_name(type_name):
    """Normalize a type name, e.g.

    envoy.foo.bar.

    Strips leading constants.ENVOY_API_NAMESPACE_PREFIX and constants.ENVOY_PREFIX.

    Args:
        type_name: a name from a TypeContext.
    Return: Normalized type name.
    """
    return normalize_field_type_name(f".{type_name}")


def type_name_from_fqn(fqn):
    return fqn[1:]


def file_cross_ref_label(msg_name):
    """File cross reference label."""
    return 'envoy_v3_api_file_%s' % msg_name


def message_cross_ref_label(msg_name):
    """Message cross reference label."""
    return 'envoy_v3_api_msg_%s' % msg_name


def enum_cross_ref_label(enum_name):
    """Enum cross reference label."""
    return 'envoy_v3_api_enum_%s' % enum_name


def field_cross_ref_label(field_name):
    """Field cross reference label."""
    return 'envoy_v3_api_field_%s' % field_name


class RstFormatVisitor(visitor.Visitor):
    """Visitor to generate a RST representation from a FileDescriptor proto.

    See visitor.Visitor for visitor method docs comments.
    """

    @cached_property
    def backticks_check(self) -> BackticksCheck:
        return BackticksCheck()

    @property
    def contrib_extension_category_data(self):
        return data["contrib_extension_categories"]

    @property
    def contrib_extension_db(self):
        return data["contrib_extensions"]

    @cached_property
    def envoy_prefixes(self) -> Set[str]:
        return set(
            [constants.ENVOY_PREFIX, constants.ENVOY_API_NAMESPACE_PREFIX, constants.CNCF_PREFIX])

    @property
    def extension_category_data(self):
        return data["extension_categories"]

    @property
    def extension_db(self):
        return data["extensions"]

    @property
    def extension_security_postures(self):
        return data["extension_security_postures"]

    @property
    def extension_status_values(self):
        return data["extension_status_values"]

    @cached_property
    def jinja_env(self):
        return jinja.env

    @cached_property
    def protodoc_manifest(self) -> Dict:
        return data["manifest"]

    @property
    def tpl_comment(self):
        return self.jinja_env.get_template("comment.rst.tpl")

    @property
    def tpl_content(self):
        return self.jinja_env.get_template("content.rst.tpl")

    @property
    def tpl_contrib_message(self):
        return self.jinja_env.get_template("contrib_message.rst.tpl")

    @property
    def tpl_enum(self):
        return self.jinja_env.get_template("enum.rst.tpl")

    @property
    def tpl_extension(self):
        return self.jinja_env.get_template("extension.rst.tpl")

    @property
    def tpl_extension_category(self):
        return self.jinja_env.get_template("extension_category.rst.tpl")

    @property
    def tpl_file(self):
        return self.jinja_env.get_template("file.rst.tpl")

    @property
    def tpl_header(self):
        return self.jinja_env.get_template("header.rst.tpl")

    @property
    def tpl_message(self):
        return self.jinja_env.get_template("message.rst.tpl")

    @property
    def tpl_security(self):
        return self.jinja_env.get_template("security.rst.tpl")

    def visit_enum(self, enum_proto, ctx) -> str:
        if self._hide(ctx.leading_comment.annotations):
            return ''
        name = normalize_type_context_name(ctx.name)
        return self.tpl_content.render(
            header=self.tpl_header.render(
                anchor=enum_cross_ref_label(name),
                title=f"Enum {name}",
                proto_link=github_url(f"[{name} proto]", ctx),
                comment=self._comment(ctx.leading_comment)),
            body=self.tpl_enum.render(enum_items=self._enums(ctx, enum_proto)))

    def visit_file(self, file_proto, ctx, services, msgs, enums) -> str:
        # If there is a file-level 'not-implemented-hide' annotation then return empty string.
        if self._hide(ctx.source_code_info.file_level_annotations):
            return ''
        body = self.tpl_file.render(msgs=msgs, enums=enums)
        has_messages = bool(body.strip())
        if self._missing_title(has_messages, file_proto, ctx):
            raise ProtodocError(
                'Envoy API proto file missing [#protodoc-title:] annotation: {}'.format(
                    file_proto.name))
        # Find the earliest detached comment, attribute it to file level.
        # Also extract file level titles if any.
        return self.tpl_content.render(
            header=self._header_from_file(ctx.source_code_info, file_proto, has_messages),
            body=body)

    def visit_message(self, msg_proto, ctx, nested_msgs: Iterable, nested_enums: Iterable) -> str:
        # Skip messages synthesized to represent map types.
        if msg_proto.options.map_entry or self._hide(ctx.leading_comment.annotations):
            return ''
        name = normalize_type_context_name(ctx.name)
        return self.tpl_content.render(
            header=self.tpl_header.render(
                anchor=message_cross_ref_label(name),
                title=name,
                proto_link=github_url(f"[{name} proto]", ctx),
                comment=self._comment(
                    ctx.leading_comment,
                    msg_proto.options.HasExtension(xds_status_pb2.message_status) and
                    msg_proto.options.Extensions[xds_status_pb2.message_status].work_in_progress)),
            body=self.tpl_message.render(
                pretty_label_names=constants.FIELD_LABEL_NAMES,
                json_values=self._json_values(msg_proto, ctx),
                msgs=self._messages(ctx, msg_proto),
                nested_msgs=nested_msgs,
                nested_enums=nested_enums))

    @lru_cache
    def _comment(self, comment, show_wip_warning=False):
        """Format a comment string with additional RST for annotations.
        """
        return self.tpl_comment.render(
            comment=annotations.without_annotations(comment.raw),
            wip_warning=WIP_WARNING if show_wip_warning else "",
            extension=(
                self._extension(extension) if
                (extension := comment.annotations.get(annotations.EXTENSION_ANNOTATION)) else ""),
            categories=self._extension_categories(comment))

    def _enum(self, index, ctx, enum_value) -> Dict:
        """Format a EnumValueDescriptorProto as RST definition list item.
        """
        ctx = ctx.extend_enum_value(index, enum_value.name)
        if self._hide(ctx.leading_comment.annotations):
            return ''
        # move this to template
        default_comment = '*(DEFAULT)* ' if enum_value.number == 0 else ''
        comment = default_comment + constants.UNICODE_INVISIBLE_SEPARATOR + self._comment(
            ctx.leading_comment)
        return dict(
            anchor=self._enum_anchor(normalize_type_context_name(ctx.name)),
            value=enum_value,
            comment=comment)

    def _enum_anchor(self, name) -> str:
        """Enum value cross reference label."""
        return f"envoy_v3_api_enum_value_{name}"

    def _enums(self, ctx, enum) -> Iterator[Dict]:
        """Format a EnumDescriptorProto as RST definition list.
        """
        for i, enum_value in enumerate(enum.value):
            if item := self._enum(i, ctx, enum_value):
                yield item

    def _extension(self, extension):
        """Format extension metadata as RST.
        """
        try:
            extension_metadata = self.extension_db.get(extension, None)
            contrib = ''
            if extension_metadata is None:
                extension_metadata = self.contrib_extension_db[extension]
                contrib = f"{self.tpl_contrib_message.render()}\n"
            status = (self.extension_status_values.get(extension_metadata.get('status'))
                      or '').strip()
            security_posture = self.extension_security_postures[
                extension_metadata['security_posture']].strip()
            categories = extension_metadata["categories"]
            type_urls = extension_metadata.get("type_urls", [])
        except KeyError as e:
            sys.stderr.write(
                f"\n\nDid you forget to add '{extension}' to extensions_build_config.bzl, "
                "extensions_metadata.yaml, contrib_build_config.bzl, "
                "or contrib/extensions_metadata.yaml?\n\n")
            exit(1)  # Raising the error buries the above message in tracebacks.

        rendered_ext = self.tpl_extension.render(
            extension=extension,
            contrib=contrib,
            status=status,
            security_posture=security_posture,
            categories=categories,
            type_urls=type_urls)
        return f"{rendered_ext}\n"

    def _extension_categories(self, comment):
        formatted_extension_category = ''
        if annotations.EXTENSION_CATEGORY_ANNOTATION in comment.annotations:
            for category in comment.annotations[annotations.EXTENSION_CATEGORY_ANNOTATION].split(
                    ","):
                yield self._extension_category(category)

    def _extension_category(self, extension_category):
        """Format extension metadata as RST.
        """
        extensions = self.extension_category_data.get(extension_category, [])
        contrib_extensions = self.contrib_extension_category_data.get(extension_category, [])
        if not extensions and not contrib_extensions:
            raise ProtodocError(f"\n\nUnable to find extension category:  {extension_category}\n\n")
        return self.tpl_extension_category.render(
            category=extension_category,
            extensions=sorted(extensions),
            contrib_extensions=sorted(contrib_extensions))

    def _field_is_required(self, field) -> bool:
        if not field.options.HasExtension(validate_pb2.rules):
            return False
        rule = field.options.Extensions[validate_pb2.rules]
        return ((rule.HasField('message') and rule.message.required)
                or (rule.HasField('duration') and rule.duration.required)
                or (rule.HasField('string') and rule.string.min_len > 0)
                or (rule.HasField('string') and rule.string.min_bytes > 0)
                or (rule.HasField('repeated') and rule.repeated.min_items > 0))

    def _field_security_options(self, field, type_context):
        # If there is a udpa.annotations.security option, include it after the comment.
        if not field.options.HasExtension(security_pb2.security):
            return ""
        security_option = field.options.Extensions[security_pb2.security]
        return [
            self._security_option(
                type_context.name, security_option.configure_for_untrusted_upstream,
                security_option.configure_for_untrusted_downstream)
        ]

    def _field_type(self, typenames, field_type, type_name):
        """Format a FieldDescriptorProto type description.

        Adds cross-refs for message types.
        TODO(htuch): Add cross-refs for enums as well.
        """
        if type_name.startswith(constants.WKT_NAMESPACE_PREFIX):
            return self._field_type_wkt(type_name)
        elif type_name.startswith(constants.RPC_NAMESPACE_PREFIX):
            return self._field_type_rpc(type_name)
        elif self._is_envoy_proto(type_name):
            if resolved := self._field_type_envoy(typenames, field_type, type_name):
                return resolved
        elif type_name:
            return type_name
        if field_type in constants.FIELD_TYPE_NAMES:
            return format_external_link(
                constants.FIELD_TYPE_NAMES[field_type],
                'https://developers.google.com/protocol-buffers/docs/proto#scalar')
        raise ProtodocError('Unknown field type ' + str(field_type))

    def _field_type_envoy(self, typenames, field_type, type_name):
        normal_type_name = normalize_field_type_name(type_name)
        if field_type == field_proto.TYPE_MESSAGE:
            # _type_name = type_name_from_fqn(normal_type_name)
            _type_name = type_name_from_fqn(type_name)
            if _type := typenames.get(_type_name):
                k, v = _type
                return f'map<{self._field_type(typenames, k.type, k.type_name)}, {self._field_type(typenames, v.type, v.type_name)}>'
            return format_internal_link(normal_type_name, message_cross_ref_label(normal_type_name))
        if field_type == field_proto.TYPE_ENUM:
            return format_internal_link(normal_type_name, enum_cross_ref_label(normal_type_name))

    @lru_cache
    def _field_type_rpc(self, type_name) -> str:
        rpc = type_name[len(constants.RPC_NAMESPACE_PREFIX):]
        return format_external_link(
            rpc, 'https://cloud.google.com/natural-language/docs/reference/rpc/google.rpc#%s'
            % rpc.lower())

    @lru_cache
    def _field_type_wkt(self, type_name) -> str:
        wkt = type_name[len(constants.WKT_NAMESPACE_PREFIX):]
        return format_external_link(
            wkt,
            f"https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#{wkt.lower()}"
        )

    def _header_from_file(self, source_code_info, file_proto, has_messages) -> str:
        """Format RST header based on special file level title
        """
        proto_name = file_proto.name
        stripped_comment = annotations.without_annotations(
            '\n'.join(c + '\n' for c in source_code_info.file_level_comments))
        formatted_extension = (
            self._extension(
                source_code_info.file_level_annotations[annotations.EXTENSION_ANNOTATION])
            if annotations.EXTENSION_ANNOTATION in source_code_info.file_level_annotations else "")
        title = (
            source_code_info.file_level_annotations[annotations.DOC_TITLE_ANNOTATION]
            if annotations.DOC_TITLE_ANNOTATION in source_code_info.file_level_annotations else
            proto_name)
        return self.tpl_header.render(
            anchor=file_cross_ref_label(proto_name),
            title=f"{title} (proto)",
            style="=",
            orphan=not has_messages,
            extension=formatted_extension,
            comment=stripped_comment,
            warnings=self._warnings(file_proto))

    def _hide(self, _annotations) -> bool:
        """Should a given type_context.Comment be hidden because it is tagged as [#not-implemented-hide:]?"""
        return annotations.NOT_IMPLEMENTED_HIDE_ANNOTATION in _annotations

    def _is_envoy_proto(self, type_name) -> bool:
        return any(type_name.startswith(prefix) for prefix in self.envoy_prefixes)

    def _json_value(self, ctx, field) -> str:
        """Format FieldDescriptorProto.Type as a pseudo-JSON string.
        """
        if type_name_from_fqn(field.type_name) in ctx.map_typenames:
            return '{...}'
        if field.label == field.LABEL_REPEATED:
            return '[]'
        if field.type == field.TYPE_MESSAGE:
            return '{...}'
        return '...'

    def _json_values(self, msg_proto, ctx) -> Iterator[Tuple[str, str]]:
        for i, field in enumerate(msg_proto.field):
            if self._hide(ctx.extend_field(i, field.name).leading_comment.annotations):
                continue
            yield field.name, self._json_value(ctx, field)

    def _message(self, outer_ctx, ctx, field) -> Dict:
        """Format a FieldDescriptorProto as RST definition list item.
        """
        if self._hide(ctx.leading_comment.annotations):
            return {}

        field_annotations = []

        if self._field_is_required(field):
            field_annotations = ['*REQUIRED*']

        formatted_leading_comment = self._comment(
            ctx.leading_comment,
            field.options.HasExtension(xds_status_pb2.field_status)
            and field.options.Extensions[xds_status_pb2.field_status].work_in_progress)

        if field.HasField('oneof_index'):
            oneof_context = outer_ctx.extend_oneof(
                field.oneof_index, ctx.oneof_names[field.oneof_index])
            if self._hide(oneof_context.leading_comment.annotations):
                return {}
            oneof_comment = oneof_context.leading_comment
            formatted_oneof_comment = self._comment(oneof_comment)
            formatted_leading_comment = (
                formatted_leading_comment.strip()
                if not formatted_leading_comment.strip() else formatted_leading_comment)

            # If the oneof only has one field and marked required, mark the field as required.
            if len(ctx.oneof_fields[field.oneof_index]) == 1 and ctx.oneof_required[
                    field.oneof_index]:
                field_annotations = ['*REQUIRED*']

            if len(ctx.oneof_fields[field.oneof_index]) > 1:
                # Fields in oneof shouldn't be marked as required when we have oneof comment below it.
                field_annotations = []
                oneof_template = '\nPrecisely one of %s must be set.\n' if ctx.oneof_required[
                    field.oneof_index] else '\nOnly one of %s may be set.\n'
                formatted_oneof_comment += oneof_template % ', '.join(
                    format_internal_link(
                        f,
                        field_cross_ref_label(
                            normalize_type_context_name(outer_ctx.extend_field(i, f).name)))
                    for i, f in ctx.oneof_fields[field.oneof_index])
        else:
            formatted_oneof_comment = ''

        security_options = self._field_security_options(field, ctx)

        return dict(
            anchor=field_cross_ref_label(normalize_type_context_name(ctx.name)),
            field=field,
            field_name=field.name,
            comment=self._field_type(ctx.map_typenames, field.type, field.type_name),
            field_annotations=",".join(field_annotations),
            formatted_leading_comment=formatted_leading_comment,
            formatted_oneof_comment=formatted_oneof_comment,
            security_options=security_options)

    def _messages(self, type_context, msg):
        type_context.oneof_fields = defaultdict(list)
        type_context.oneof_required = defaultdict(bool)
        type_context.oneof_names = defaultdict(list)
        for index, field in enumerate(msg.field):
            if field.HasField('oneof_index'):
                leading_comment = type_context.extend_field(index, field.name).leading_comment
                if self._hide(leading_comment.annotations):
                    continue
                type_context.oneof_fields[field.oneof_index].append((index, field.name))
        for index, oneof_decl in enumerate(msg.oneof_decl):
            if oneof_decl.options.HasExtension(validate_pb2.required):
                type_context.oneof_required[index] = oneof_decl.options.Extensions[
                    validate_pb2.required]
            type_context.oneof_names[index] = oneof_decl.name
        for index, field in enumerate(msg.field):
            item = self._message(type_context, type_context.extend_field(index, field.name), field)
            if item:
                yield item

    def _missing_title(self, has_messages, file_proto, ctx):
        # TODO(mattklein123): The logic in both the doc and transform tool around files without messages
        # is confusing and should be cleaned up. This is a stop gap to have titles for all proto docs
        # in the common case.
        return (
            has_messages
            and not annotations.DOC_TITLE_ANNOTATION in ctx.source_code_info.file_level_annotations
            and file_proto.name.startswith('envoy'))

    def _security_option(self, name, untrusted_upstream, untrusted_downstream):
        if not (config := self.protodoc_manifest.get(name)):
            raise ProtodocError('Missing protodoc manifest YAML for %s' % name)
        return self.tpl_security.render(
            untrusted_downstream=untrusted_downstream,
            untrusted_upstream=untrusted_upstream,
            note=config["note"].strip(),
            example=yaml.dump({name.split('.')[-1]: config["example"]}))

    def _warnings(self, file_proto):
        _warnings = ((
            file_proto.options.HasExtension(udpa_status_pb2.file_status)
            and file_proto.options.Extensions[udpa_status_pb2.file_status].work_in_progress) or (
                file_proto.options.HasExtension(xds_status_pb2.file_status)
                and file_proto.options.Extensions[xds_status_pb2.file_status].work_in_progress))
        return (WIP_WARNING if _warnings else "")


def main():
    plugin.plugin([plugin.direct_output_descriptor('.rst', RstFormatVisitor)])


if __name__ == '__main__':
    main()
