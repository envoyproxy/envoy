"""FileDescriptorProto traversal for api_proto_plugin framework."""

from tools.api_proto_plugin import type_context


def TraverseService(type_context, service_proto, visitor):
  """Traverse a service definition.

  Args:
    type_context: type_context.TypeContext for service type.
    service_proto: ServiceDescriptorProto for service.
    visitor: visitor.Visitor defining the business logic of the plugin.

  Returns:
    Plugin specific output.
  """
  return visitor.VisitService(service_proto, type_context)


def TraverseEnum(type_context, enum_proto, visitor):
  """Traverse an enum definition.

  Args:
    type_context: type_context.TypeContext for enum type.
    enum_proto: EnumDescriptorProto for enum.
    visitor: visitor.Visitor defining the business logic of the plugin.

  Returns:
    Plugin specific output.
  """
  return visitor.VisitEnum(enum_proto, type_context)


def TraverseMessage(type_context, msg_proto, visitor):
  """Traverse a message definition.

  Args:
    type_context: type_context.TypeContext for message type.
    msg_proto: DescriptorProto for message.
    visitor: visitor.Visitor defining the business logic of the plugin.

  Returns:
    Plugin specific output.
  """
  # We need to do some extra work to recover the map type annotation from the
  # synthesized messages.
  type_context.map_typenames = {
      '%s.%s' % (type_context.name, nested_msg.name): (nested_msg.field[0], nested_msg.field[1])
      for nested_msg in msg_proto.nested_type
      if nested_msg.options.map_entry
  }
  nested_msgs = [
      TraverseMessage(
          type_context.ExtendNestedMessage(index, nested_msg.name, nested_msg.options.deprecated),
          nested_msg, visitor) for index, nested_msg in enumerate(msg_proto.nested_type)
  ]
  nested_enums = [
      TraverseEnum(
          type_context.ExtendNestedEnum(index, nested_enum.name, nested_enum.options.deprecated),
          nested_enum, visitor) for index, nested_enum in enumerate(msg_proto.enum_type)
  ]
  return visitor.VisitMessage(msg_proto, type_context, nested_msgs, nested_enums)


def TraverseFile(file_proto, visitor):
  """Traverse a proto file definition.

  Args:
    file_proto: FileDescriptorProto for file.
    visitor: visitor.Visitor defining the business logic of the plugin.

  Returns:
    Plugin specific output.
  """
  source_code_info = type_context.SourceCodeInfo(file_proto.name, file_proto.source_code_info)
  package_type_context = type_context.TypeContext(source_code_info, file_proto.package)
  services = [
      TraverseService(package_type_context.ExtendService(index, service.name), service, visitor)
      for index, service in enumerate(file_proto.service)
  ]
  msgs = [
      TraverseMessage(package_type_context.ExtendMessage(index, msg.name, msg.options.deprecated),
                      msg, visitor) for index, msg in enumerate(file_proto.message_type)
  ]
  enums = [
      TraverseEnum(package_type_context.ExtendEnum(index, enum.name, enum.options.deprecated), enum,
                   visitor) for index, enum in enumerate(file_proto.enum_type)
  ]
  return visitor.VisitFile(file_proto, package_type_context, services, msgs, enums)
