import unittest

import merge_active_shadow

from tools.api_proto_plugin import type_context as api_type_context

from google.protobuf import descriptor_pb2
from google.protobuf import text_format


class MergeActiveShadowTest(unittest.TestCase):
  # Dummy type context for tests that don't care about this.
  def fakeTypeContext(self):
    fake_source_code_info = descriptor_pb2.SourceCodeInfo()
    source_code_info = api_type_context.SourceCodeInfo('fake', fake_source_code_info)
    return api_type_context.TypeContext(source_code_info, 'fake_package')

  # Poor man's text proto equivalence. Tensorflow has better tools for this,
  # i.e. assertProto2Equal.
  def assertTextProtoEq(self, lhs, rhs):
    self.assertMultiLineEqual(lhs.strip(), rhs.strip())

  def testAdjustReservedRange(self):
    """AdjustReservedRange removes specified skip_reserved_numbers."""
    desc_pb_text = """
reserved_range {
  start: 41
  end: 41
}
reserved_range {
  start: 42
  end: 42
}
reserved_range {
  start: 43
  end: 44
}
reserved_range {
  start: 50
  end: 51
}
    """
    desc = descriptor_pb2.DescriptorProto()
    text_format.Merge(desc_pb_text, desc)
    target = descriptor_pb2.DescriptorProto()
    merge_active_shadow.AdjustReservedRange(target, desc.reserved_range, [42, 43])
    target_pb_text = """
reserved_range {
  start: 41
  end: 41
}
reserved_range {
  start: 50
  end: 51
}
    """
    self.assertTextProtoEq(target_pb_text, str(target))

  def testMergeActiveShadowEnum(self):
    """MergeActiveShadowEnum recovers shadow values."""
    active_pb_text = """
value {
  number: 1
  name: "foo"
}
value {
  number: 0
  name: "DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE"
}
value {
  number: 3
  name: "bar"
}
reserved_name: "baz"
reserved_range {
  start: 2
  end: 3
}
    """
    active_proto = descriptor_pb2.EnumDescriptorProto()
    text_format.Merge(active_pb_text, active_proto)
    shadow_pb_text = """
value {
  number: 1
  name: "foo"
}
value {
  number: 0
  name: "wow"
}
value {
  number: 3
  name: "bar"
}
value {
  number: 2
  name: "hidden_envoy_deprecated_baz"
}
value {
  number: 4
  name: "hidden_envoy_deprecated_huh"
}
    """
    shadow_proto = descriptor_pb2.EnumDescriptorProto()
    text_format.Merge(shadow_pb_text, shadow_proto)
    target_proto = descriptor_pb2.EnumDescriptorProto()
    merge_active_shadow.MergeActiveShadowEnum(active_proto, shadow_proto, target_proto)
    target_pb_text = """
value {
  name: "foo"
  number: 1
}
value {
  name: "wow"
  number: 0
}
value {
  name: "bar"
  number: 3
}
value {
  name: "hidden_envoy_deprecated_baz"
  number: 2
}
    """
    self.assertTextProtoEq(target_pb_text, str(target_proto))

  def testMergeActiveShadowMessageComments(self):
    """MergeActiveShadowMessage preserves comment field correspondence."""
    active_pb_text = """
field {
  number: 9
  name: "oneof_1_0"
  oneof_index: 0
}
field {
  number: 1
  name: "simple_field_0"
}
field {
  number: 0
  name: "oneof_2_0"
  oneof_index: 2
}
field {
  number: 8
  name: "oneof_2_1"
  oneof_index: 2
}
field {
  number: 3
  name: "oneof_0_0"
  oneof_index: 1
}
field {
  number: 4
  name: "newbie"
}
field {
  number: 7
  name: "oneof_3_0"
  oneof_index: 3
}
reserved_name: "missing_oneof_field_0"
reserved_name: "missing_oneof_field_1"
reserved_name: "missing_oneof_field_2"
oneof_decl {
  name: "oneof_0"
}
oneof_decl {
  name: "oneof_1"
}
oneof_decl {
  name: "oneof_2"
}
oneof_decl {
  name: "oneof_3"
}
    """
    active_proto = descriptor_pb2.DescriptorProto()
    text_format.Merge(active_pb_text, active_proto)
    active_source_code_info_text = """
location {
  path: [4, 1, 2, 4]
  leading_comments: "field_4"
}
location {
  path: [4, 1, 2, 5]
  leading_comments: "field_5"
}
location {
  path: [4, 1, 2, 3]
  leading_comments: "field_3"
}
location {
  path: [4, 1, 2, 0]
  leading_comments: "field_0"
}
location {
  path: [4, 1, 2, 1]
  leading_comments: "field_1"
}
location {
  path: [4, 0, 2, 2]
  leading_comments: "ignore_0"
}
location {
  path: [4, 1, 2, 6]
  leading_comments: "field_6"
}
location {
  path: [4, 1, 2, 2]
  leading_comments: "field_2"
}
location {
  path: [3]
  leading_comments: "ignore_1"
}
"""
    active_source_code_info = descriptor_pb2.SourceCodeInfo()
    text_format.Merge(active_source_code_info_text, active_source_code_info)
    shadow_pb_text = """
field {
  number: 10
  name: "hidden_envoy_deprecated_missing_oneof_field_0"
  oneof_index: 0
}
field {
  number: 11
  name: "hidden_envoy_deprecated_missing_oneof_field_1"
  oneof_index: 3
}
field {
  number: 11
  name: "hidden_envoy_deprecated_missing_oneof_field_2"
  oneof_index: 2
}
oneof_decl {
  name: "oneof_0"
}
oneof_decl {
  name: "oneof_1"
}
oneof_decl {
  name: "oneof_2"
}
oneof_decl {
  name: "some_removed_oneof"
}
oneof_decl {
  name: "oneof_3"
}
"""
    shadow_proto = descriptor_pb2.DescriptorProto()
    text_format.Merge(shadow_pb_text, shadow_proto)
    target_proto = descriptor_pb2.DescriptorProto()
    source_code_info = api_type_context.SourceCodeInfo('fake', active_source_code_info)
    fake_type_context = api_type_context.TypeContext(source_code_info, 'fake_package')
    merge_active_shadow.MergeActiveShadowMessage(fake_type_context.ExtendMessage(1, "foo", False),
                                                 active_proto, shadow_proto, target_proto)
    target_pb_text = """
field {
  name: "oneof_1_0"
  number: 9
  oneof_index: 0
}
field {
  name: "hidden_envoy_deprecated_missing_oneof_field_0"
  number: 10
  oneof_index: 0
}
field {
  name: "simple_field_0"
  number: 1
}
field {
  name: "oneof_2_0"
  number: 0
  oneof_index: 2
}
field {
  name: "oneof_2_1"
  number: 8
  oneof_index: 2
}
field {
  name: "hidden_envoy_deprecated_missing_oneof_field_2"
  number: 11
  oneof_index: 2
}
field {
  name: "oneof_0_0"
  number: 3
  oneof_index: 1
}
field {
  name: "newbie"
  number: 4
}
field {
  name: "oneof_3_0"
  number: 7
  oneof_index: 3
}
field {
  name: "hidden_envoy_deprecated_missing_oneof_field_1"
  number: 11
  oneof_index: 4
}
oneof_decl {
  name: "oneof_0"
}
oneof_decl {
  name: "oneof_1"
}
oneof_decl {
  name: "oneof_2"
}
oneof_decl {
  name: "oneof_3"
}
oneof_decl {
  name: "some_removed_oneof"
}
    """
    target_source_code_info_text = """
location {
  path: 4
  path: 1
  path: 2
  path: 6
  leading_comments: "field_4"
}
location {
  path: 4
  path: 1
  path: 2
  path: 7
  leading_comments: "field_5"
}
location {
  path: 4
  path: 1
  path: 2
  path: 4
  leading_comments: "field_3"
}
location {
  path: 4
  path: 1
  path: 2
  path: 0
  leading_comments: "field_0"
}
location {
  path: 4
  path: 1
  path: 2
  path: 2
  leading_comments: "field_1"
}
location {
  path: 4
  path: 0
  path: 2
  path: 2
  leading_comments: "ignore_0"
}
location {
  path: 4
  path: 1
  path: 2
  path: 8
  leading_comments: "field_6"
}
location {
  path: 4
  path: 1
  path: 2
  path: 3
  leading_comments: "field_2"
}
location {
  path: 3
  leading_comments: "ignore_1"
}
"""
    self.maxDiff = None
    self.assertTextProtoEq(target_pb_text, str(target_proto))
    self.assertTextProtoEq(target_source_code_info_text,
                           str(fake_type_context.source_code_info.proto))

  def testMergeActiveShadowMessage(self):
    """MergeActiveShadowMessage recovers shadow fields with oneofs."""
    active_pb_text = """
field {
  number: 1
  name: "foo"
}
field {
  number: 0
  name: "bar"
  oneof_index: 2
}
field {
  number: 3
  name: "baz"
}
field {
  number: 4
  name: "newbie"
}
reserved_name: "wow"
reserved_range {
  start: 2
  end: 3
}
oneof_decl {
  name: "ign"
}
oneof_decl {
  name: "ign2"
}
oneof_decl {
  name: "some_oneof"
}
    """
    active_proto = descriptor_pb2.DescriptorProto()
    text_format.Merge(active_pb_text, active_proto)
    shadow_pb_text = """
field {
  number: 1
  name: "foo"
}
field {
  number: 0
  name: "bar"
}
field {
  number: 3
  name: "baz"
}
field {
  number: 2
  name: "hidden_envoy_deprecated_wow"
  oneof_index: 0
}
oneof_decl {
  name: "some_oneof"
}
    """
    shadow_proto = descriptor_pb2.DescriptorProto()
    text_format.Merge(shadow_pb_text, shadow_proto)
    target_proto = descriptor_pb2.DescriptorProto()
    merge_active_shadow.MergeActiveShadowMessage(self.fakeTypeContext(), active_proto, shadow_proto,
                                                 target_proto)
    target_pb_text = """
field {
  name: "foo"
  number: 1
}
field {
  name: "bar"
  number: 0
  oneof_index: 2
}
field {
  name: "hidden_envoy_deprecated_wow"
  number: 2
  oneof_index: 2
}
field {
  name: "baz"
  number: 3
}
field {
  name: "newbie"
  number: 4
}
oneof_decl {
  name: "ign"
}
oneof_decl {
  name: "ign2"
}
oneof_decl {
  name: "some_oneof"
}
    """
    self.assertTextProtoEq(target_pb_text, str(target_proto))

  def testMergeActiveShadowMessageNoShadowMessage(self):
    """MergeActiveShadowMessage doesn't require a shadow message for new nested active messages."""
    active_proto = descriptor_pb2.DescriptorProto()
    shadow_proto = descriptor_pb2.DescriptorProto()
    active_proto.nested_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    merge_active_shadow.MergeActiveShadowMessage(self.fakeTypeContext(), active_proto, shadow_proto,
                                                 target_proto)
    self.assertEqual(target_proto.nested_type[0].name, 'foo')

  def testMergeActiveShadowMessageNoShadowEnum(self):
    """MergeActiveShadowMessage doesn't require a shadow enum for new nested active enums."""
    active_proto = descriptor_pb2.DescriptorProto()
    shadow_proto = descriptor_pb2.DescriptorProto()
    active_proto.enum_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    merge_active_shadow.MergeActiveShadowMessage(self.fakeTypeContext(), active_proto, shadow_proto,
                                                 target_proto)
    self.assertEqual(target_proto.enum_type[0].name, 'foo')

  def testMergeActiveShadowMessageMissing(self):
    """MergeActiveShadowMessage recovers missing messages from shadow."""
    active_proto = descriptor_pb2.DescriptorProto()
    shadow_proto = descriptor_pb2.DescriptorProto()
    shadow_proto.nested_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    merge_active_shadow.MergeActiveShadowMessage(self.fakeTypeContext(), active_proto, shadow_proto,
                                                 target_proto)
    self.assertEqual(target_proto.nested_type[0].name, 'foo')

  def testMergeActiveShadowFileMissing(self):
    """MergeActiveShadowFile recovers missing messages from shadow."""
    active_proto = descriptor_pb2.FileDescriptorProto()
    shadow_proto = descriptor_pb2.FileDescriptorProto()
    shadow_proto.message_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    target_proto = merge_active_shadow.MergeActiveShadowFile(active_proto, shadow_proto)
    self.assertEqual(target_proto.message_type[0].name, 'foo')

  def testMergeActiveShadowFileNoShadowMessage(self):
    """MergeActiveShadowFile doesn't require a shadow message for new active messages."""
    active_proto = descriptor_pb2.FileDescriptorProto()
    shadow_proto = descriptor_pb2.FileDescriptorProto()
    active_proto.message_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    target_proto = merge_active_shadow.MergeActiveShadowFile(active_proto, shadow_proto)
    self.assertEqual(target_proto.message_type[0].name, 'foo')

  def testMergeActiveShadowFileNoShadowEnum(self):
    """MergeActiveShadowFile doesn't require a shadow enum for new active enums."""
    active_proto = descriptor_pb2.FileDescriptorProto()
    shadow_proto = descriptor_pb2.FileDescriptorProto()
    active_proto.enum_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    target_proto = merge_active_shadow.MergeActiveShadowFile(active_proto, shadow_proto)
    self.assertEqual(target_proto.enum_type[0].name, 'foo')


# TODO(htuch): add some test for recursion.

if __name__ == '__main__':
  unittest.main()
