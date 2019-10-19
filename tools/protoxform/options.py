# Manage internal options on messages/enums/fields/enum values.


def AddHideOption(options):
  """Mark message/enum/field/enum value as hidden.

  Hidden messages are ignored when generating output.

  Args:
    options: MessageOptions/EnumOptions/FieldOptions/EnumValueOptions message.
  """
  hide_option = options.uninterpreted_option.add()
  hide_option.name.add().name_part = 'protoxform_hide'


def HasHideOption(options):
  """Is message/enum/field/enum value hidden?

  Hidden messages are ignored when generating output.

  Args:
    options: MessageOptions/EnumOptions/FieldOptions/EnumValueOptions message.
  Returns:
    Hidden status.
  """
  return any(
      option.name[0].name_part == 'protoxform_hide' for option in options.uninterpreted_option)
