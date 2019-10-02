# API upgrade business logic.

import copy
import re

from tools.api_proto_plugin import visitor
from tools.protoxform import options

from google.api import annotations_pb2


def UpgradedType(t):
  return re.sub(r'(\.?envoy[\w\.]*\.)(v2alpha\d?|v2)', r'\1v3alpha', t)


def UpgradedPath(p):
  return re.sub(r'(envoy/[\w/]*/)(v2alpha\d?|v2)', r'\1v3alpha', p)


def UpgradedComment(c):
  # We approximate what needs to be done for comments by just updating anything
  # that looks like a path or comment. This isn't perfect, e.g. we miss out on
  # REST URLs etc.
  # TODO(htuch): audit and improve this.
  return UpgradedType(UpgradedPath(c))


def UpgradedPostMethod(m):
  return re.sub(r'^/v2/', '/v3alpha/', m)


def UpgradeService(service_proto):
  """In-place upgrade a ServiceDescriptorProto from v2[alpha\d] to v3alpha.

  Args:
    service_proto: v2[alpha\d] ServiceDescriptorProto message.
  """
  for m in service_proto.method:
    if m.options.HasExtension(annotations_pb2.http):
      http_options = m.options.Extensions[annotations_pb2.http]
      http_options.post = UpgradedPostMethod(http_options.post)
    m.input_type = UpgradedType(m.input_type)
    m.output_type = UpgradedType(m.output_type)


def Deprecate(proto, field_or_value):
  """Deprecate a field or value in a message/enum proto.

  Args:
    proto: DescriptorProto or EnumDescriptorProto message.
    field_or_value: field or value inside proto.
  """
  reserved = proto.reserved_range.add()
  reserved.start = field_or_value.number
  reserved.end = field_or_value.number + 1
  proto.reserved_name.append(field_or_value.name)
  options.AddHideOption(field_or_value.options)


def UpgradeMessage(msg_proto):
  """In-place upgrade a DescriptorProto from v2[alpha\d] to v3alpha.

  Args:
    msg_proto: v2[alpha\d] DescriptorProto message.
  """
  if msg_proto.options.deprecated:
    options.AddHideOption(msg_proto.options)
  for f in msg_proto.field:
    if f.options.deprecated:
      Deprecate(msg_proto, f)
    else:
      f.type_name = UpgradedType(f.type_name)
  for m in msg_proto.nested_type:
    UpgradeMessage(m)
  for e in msg_proto.enum_type:
    UpgradeEnum(e)


def UpgradeEnum(enum_proto):
  """In-place upgrade an EnumDescriptorProto from v2[alpha\d] to v3alpha.

  Args:
    enum_proto: v2[alpha\d] EnumDescriptorProto message.
  """
  for v in enum_proto.value:
    if v.options.deprecated:
      # We need special handling for the zero field, as proto3 needs some value
      # here.
      if v.number == 0:
        v.name = 'DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE'
      else:
        Deprecate(enum_proto, v)


def UpgradeFile(file_proto):
  """In-place upgrade a FileDescriptorProto from v2[alpha\d] to v3alpha.

  Args:
    file_proto: v2[alpha\d] FileDescriptorProto message.
  """
  # Upgrade package.
  file_proto.package = UpgradedType(file_proto.package)
  # Upgrade imports.
  for n, d in enumerate(file_proto.dependency):
    file_proto.dependency[n] = UpgradedPath(d)
  # Upgrade comments.
  for location in file_proto.source_code_info.location:
    location.leading_comments = UpgradedComment(location.leading_comments)
    location.trailing_comments = UpgradedComment(location.trailing_comments)
    for n, c in enumerate(location.leading_detached_comments):
      location.leading_detached_comments[n] = UpgradedComment(c)
  # Upgrade services.
  for s in file_proto.service:
    UpgradeService(s)
  # Upgrade messages.
  for m in file_proto.message_type:
    UpgradeMessage(m)
  for e in file_proto.enum_type:
    UpgradeEnum(e)
  return file_proto


def V3MigrationXform(file_proto):
  """Transform a FileDescriptorProto from v2[alpha\d] to v3alpha.

  Args:
    file_proto: v2[alpha\d] FileDescriptorProto message.

  Returns:
    v3 FileDescriptorProto message.
  """
  return UpgradeFile(copy.deepcopy(file_proto))
