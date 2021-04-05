"""FileDescriptorProto traversal for api_proto_plugin framework."""

from tools.api_proto_plugin import type_context


def traverse_service(type_context, service_proto, visitor):
    """Traverse a service definition.

    Args:
        type_context: type_context.TypeContext for service type.
        service_proto: ServiceDescriptorProto for service.
        visitor: visitor.Visitor defining the business logic of the plugin.

    Returns:
        Plugin specific output.
    """
    return visitor.visit_service(service_proto, type_context)


def traverse_enum(type_context, enum_proto, visitor):
    """Traverse an enum definition.

    Args:
        type_context: type_context.TypeContext for enum type.
        enum_proto: EnumDescriptorProto for enum.
        visitor: visitor.Visitor defining the business logic of the plugin.

    Returns:
        Plugin specific output.
    """
    return visitor.visit_enum(enum_proto, type_context)


def traverse_message(type_context, msg_proto, visitor):
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
        traverse_message(
            type_context.extend_nested_message(
                index, nested_msg.name, nested_msg.options.deprecated), nested_msg, visitor)
        for index, nested_msg in enumerate(msg_proto.nested_type)
    ]
    nested_enums = [
        traverse_enum(
            type_context.extend_nested_enum(
                index, nested_enum.name, nested_enum.options.deprecated), nested_enum, visitor)
        for index, nested_enum in enumerate(msg_proto.enum_type)
    ]
    return visitor.visit_message(msg_proto, type_context, nested_msgs, nested_enums)


def traverse_file(file_proto, visitor):
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
        traverse_service(
            package_type_context.extend_service(index, service.name), service, visitor)
        for index, service in enumerate(file_proto.service)
    ]
    msgs = [
        traverse_message(
            package_type_context.extend_message(index, msg.name, msg.options.deprecated), msg,
            visitor) for index, msg in enumerate(file_proto.message_type)
    ]
    enums = [
        traverse_enum(
            package_type_context.extend_enum(index, enum.name, enum.options.deprecated), enum,
            visitor) for index, enum in enumerate(file_proto.enum_type)
    ]
    return visitor.visit_file(file_proto, package_type_context, services, msgs, enums)
