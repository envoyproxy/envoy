# Merge active and previous version's generated next major version candidate
# shadow. This involve simultaneously traversing both FileDescriptorProtos and:
# 1. Recovering hidden_envoy_depreacted_* fields and enum values in active proto.
# 2. Recovering deprecated (sub)message types.
# 3. Misc. fixups for oneof metadata and reserved ranges/names.

import copy
import pathlib
import sys

from google.protobuf import descriptor_pb2
from google.protobuf import text_format

# Note: we have to include those proto definitions for text_format sanity.
from google.api import annotations_pb2 as _
from validate import validate_pb2 as _
from envoy.annotations import deprecation_pb2 as _
from envoy.annotations import resource_pb2 as _
from udpa.annotations import migrate_pb2 as _
from udpa.annotations import sensitive_pb2 as _
from udpa.annotations import status_pb2 as _
from udpa.annotations import versioning_pb2 as _


# Set reserved_range in target_proto to reflex previous_reserved_range skipping
# skip_reserved_numbers.
def AdjustReservedRange(target_proto, previous_reserved_range, skip_reserved_numbers):
  del target_proto.reserved_range[:]
  for rr in previous_reserved_range:
    # We can only handle singleton ranges today.
    assert ((rr.start == rr.end) or (rr.end == rr.start + 1))
    if rr.start not in skip_reserved_numbers:
      target_proto.reserved_range.add().MergeFrom(rr)


# Merge active/shadow EnumDescriptorProtos to a fresh target EnumDescriptorProto.
def MergeActiveShadowEnum(active_proto, shadow_proto, target_proto):
  target_proto.MergeFrom(active_proto)
  shadow_values = {v.name: v for v in shadow_proto.value}
  skip_reserved_numbers = []
  # For every reserved name, check to see if it's in the shadow, and if so,
  # reintroduce in target_proto.
  del target_proto.reserved_name[:]
  for n in active_proto.reserved_name:
    hidden_n = 'hidden_envoy_deprecated_' + n
    if hidden_n in shadow_values:
      v = shadow_values[hidden_n]
      skip_reserved_numbers.append(v.number)
      target_proto.value.add().MergeFrom(v)
    else:
      target_proto.reserved_name.append(n)
  AdjustReservedRange(target_proto, active_proto.reserved_range, skip_reserved_numbers)
  # Special fixup for deprecation of default enum values.
  for tv in target_proto.value:
    if tv.name == 'DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE':
      for sv in shadow_proto.value:
        if sv.number == tv.number:
          assert (sv.number == 0)
          tv.CopyFrom(sv)


# Merge active/shadow DescriptorProtos to a fresh target DescriptorProto.
def MergeActiveShadowMessage(active_proto, shadow_proto, target_proto):
  target_proto.MergeFrom(active_proto)
  shadow_fields = {f.name: f for f in shadow_proto.field}
  skip_reserved_numbers = []
  # For every reserved name, check to see if it's in the shadow, and if so,
  # reintroduce in target_proto.
  del target_proto.reserved_name[:]
  for n in active_proto.reserved_name:
    hidden_n = 'hidden_envoy_deprecated_' + n
    if hidden_n in shadow_fields:
      f = shadow_fields[hidden_n]
      skip_reserved_numbers.append(f.number)
      missing_field = target_proto.field.add()
      missing_field.MergeFrom(f)
      # oneof fields from the shadow need to have their index set to the
      # corresponding index in active/target_proto.
      if missing_field.HasField('oneof_index'):
        oneof_name = shadow_proto.oneof_decl[missing_field.oneof_index].name
        missing_oneof_index = None
        for oneof_index, oneof_decl in enumerate(active_proto.oneof_decl):
          if oneof_decl.name == oneof_name:
            missing_oneof_index = oneof_index
        assert (missing_oneof_index is not None)
        missing_field.oneof_index = missing_oneof_index
    else:
      target_proto.reserved_name.append(n)
  # protoprint.py expects that oneof fields are consecutive, so need to sort for
  # this.
  if len(active_proto.oneof_decl) > 0:
    fields = copy.deepcopy(target_proto.field)
    fields.sort(key=lambda f: f.oneof_index if f.HasField('oneof_index') else -1)
    del target_proto.field[:]
    for f in fields:
      target_proto.field.append(f)
  AdjustReservedRange(target_proto, active_proto.reserved_range, skip_reserved_numbers)
  # Visit nested message types
  del target_proto.nested_type[:]
  shadow_msgs = {msg.name: msg for msg in shadow_proto.nested_type}
  for msg in active_proto.nested_type:
    MergeActiveShadowMessage(msg, shadow_msgs[msg.name], target_proto.nested_type.add())
  # Visit nested enum types
  del target_proto.enum_type[:]
  shadow_enums = {msg.name: msg for msg in shadow_proto.enum_type}
  for enum in active_proto.enum_type:
    MergeActiveShadowEnum(enum, shadow_enums[enum.name], target_proto.enum_type.add())
  # Ensure target has any deprecated sub-message types in case they are needed.
  active_msg_names = set([msg.name for msg in active_proto.nested_type])
  for msg in shadow_proto.nested_type:
    if msg.name not in active_msg_names:
      target_proto.nested_type.add().MergeFrom(msg)


# Merge active/shadow FileDescriptorProtos, returning a the resulting FileDescriptorProto.
def MergeActiveShadowFile(active_file_proto, shadow_file_proto):
  target_file_proto = copy.deepcopy(active_file_proto)
  # Visit message types
  del target_file_proto.message_type[:]
  shadow_msgs = {msg.name: msg for msg in shadow_file_proto.message_type}
  for msg in active_file_proto.message_type:
    MergeActiveShadowMessage(msg, shadow_msgs[msg.name], target_file_proto.message_type.add())
  # Visit enum types
  del target_file_proto.enum_type[:]
  shadow_enums = {msg.name: msg for msg in shadow_file_proto.enum_type}
  for enum in active_file_proto.enum_type:
    MergeActiveShadowEnum(enum, shadow_enums[enum.name], target_file_proto.enum_type.add())
  # Ensure target has any deprecated message types in case they are needed.
  active_msg_names = set([msg.name for msg in active_file_proto.message_type])
  for msg in shadow_file_proto.message_type:
    if msg.name not in active_msg_names:
      target_file_proto.message_type.add().MergeFrom(msg)
  return target_file_proto


if __name__ == '__main__':
  active_src, shadow_src, dst = sys.argv[1:]
  active_proto = descriptor_pb2.FileDescriptorProto()
  text_format.Merge(pathlib.Path(active_src).read_text(), active_proto)
  shadow_proto = descriptor_pb2.FileDescriptorProto()
  text_format.Merge(pathlib.Path(shadow_src).read_text(), shadow_proto)
  pathlib.Path(dst).write_text(str(MergeActiveShadowFile(active_proto, shadow_proto)))
