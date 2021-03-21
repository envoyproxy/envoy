# Manage internal options on messages/enums/fields/enum values.

from udpa.annotations import versioning_pb2


def add_hide_option(options):
    """Mark message/enum/field/enum value as hidden.

    Hidden messages are ignored when generating output.

    Args:
        options: MessageOptions/EnumOptions/FieldOptions/EnumValueOptions message.
    """
    hide_option = options.uninterpreted_option.add()
    hide_option.name.add().name_part = 'protoxform_hide'


def has_hide_option(options):
    """Is message/enum/field/enum value hidden?

    Hidden messages are ignored when generating output.

    Args:
        options: MessageOptions/EnumOptions/FieldOptions/EnumValueOptions message.
    Returns:
        Hidden status.
    """
    return any(
        option.name[0].name_part == 'protoxform_hide' for option in options.uninterpreted_option)


def set_versioning_annotation(options, previous_message_type):
    """Set the udpa.annotations.versioning option.

    Used by Envoy to chain back through the message type history.

    Args:
        options: MessageOptions message.
        previous_message_type: string with earlier API type name for the message.
    """
    options.Extensions[versioning_pb2.versioning].previous_message_type = previous_message_type


def get_versioning_annotation(options):
    """Get the udpa.annotations.versioning option.

    Used by Envoy to chain back through the message type history.

    Args:
        options: MessageOptions message.
    Returns:
        versioning.Annotation if set otherwise None.
    """
    if not options.HasExtension(versioning_pb2.versioning):
        return None
    return options.Extensions[versioning_pb2.versioning]
