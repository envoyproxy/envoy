"""FileDescriptorProto visitor interface for api_proto_plugin implementations."""


class Visitor(object):
    """Abstract visitor interface for api_proto_plugin implementation."""

    def visit_service(self, service_proto, type_context):
        """Visit a service definition.

    Args:
      service_proto: ServiceDescriptorProto for service.
      type_context: type_context.TypeContext for service type.

    Returns:
      Plugin specific output.
    """
        pass

    def visit_enum(self, enum_proto, type_context):
        """Visit an enum definition.

    Args:
      enum_proto: EnumDescriptorProto for enum.
      type_context: type_context.TypeContext for enum type.

    Returns:
      Plugin specific output.
    """
        pass

    def visit_message(self, msg_proto, type_context, nested_msgs, nested_enums):
        """Visit a message definition.

    Args:
      msg_proto: DescriptorProto for message.
      type_context: type_context.TypeContext for message type.
      nested_msgs: a list of results from visiting nested messages.
      nested_enums: a list of results from visiting nested enums.

    Returns:
      Plugin specific output.
    """
        pass

    def visit_file(self, file_proto, type_context, services, msgs, enums):
        """Visit a proto file definition.

    Args:
      file_proto: FileDescriptorProto for file.
      type_context: type_context.TypeContext for file.
      services: a list of results from visiting services.
      msgs: a list of results from visiting messages.
      enums: a list of results from visiting enums.

    Returns:
      Plugin specific output.
    """
        pass
