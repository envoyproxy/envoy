import unittest

import merge_active_shadow

from google.protobuf import descriptor_pb2


class MergeActiveShadowTest(unittest.TestCase):

  def testAdjustReservedRange(self):
    """AdjustReservedRange removes specified skip_reserved_numbers."""
    desc = descriptor_pb2.DescriptorProto()
    rr = desc.reserved_range.add()
    rr.start, rr.end = 41, 41
    rr = desc.reserved_range.add()
    rr.start, rr.end = 42, 42
    rr = desc.reserved_range.add()
    rr.start, rr.end = 43, 44
    rr = desc.reserved_range.add()
    rr.start, rr.end = 50, 51
    target = descriptor_pb2.DescriptorProto()
    merge_active_shadow.AdjustReservedRange(target, desc.reserved_range, [42, 43])
    assert len(target.reserved_range) == 2
    assert target.reserved_range[0].start == 41
    assert target.reserved_range[1].start == 50

  def testMergeActiveShadowEnum(self):
    """MergeActiveShadowEnum recovers shadow values."""
    active_proto = descriptor_pb2.EnumDescriptorProto()
    v = active_proto.value.add()
    v.number = 1
    v.name = 'foo'
    v = active_proto.value.add()
    v.number = 0
    v.name = 'DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE'
    v = active_proto.value.add()
    v.number = 3
    v.name = 'bar'
    active_proto.reserved_name.append('baz')
    rr = active_proto.reserved_range.add()
    rr.start = 2
    rr.end = 3
    shadow_proto = descriptor_pb2.EnumDescriptorProto()
    v = shadow_proto.value.add()
    v.number = 1
    v.name = 'foo'
    v = shadow_proto.value.add()
    v.number = 0
    v.name = 'wow'
    v = shadow_proto.value.add()
    v.number = 3
    v.name = 'bar'
    v = shadow_proto.value.add()
    v.number = 2
    v.name = 'hidden_envoy_deprecated_baz'
    target_proto = descriptor_pb2.EnumDescriptorProto()
    merge_active_shadow.MergeActiveShadowEnum(active_proto, shadow_proto, target_proto)
    tv = target_proto.value
    assert len(tv) == 4
    assert tv[1].name == 'wow'
    assert tv[3].name == 'hidden_envoy_deprecated_baz'

  def testMergeActiveShadowMessage(self):
    """MergeActiveShadowMessage recovers shadow fields with oneofs."""
    active_proto = descriptor_pb2.DescriptorProto()
    f = active_proto.field.add()
    f.number = 1
    f.name = 'foo'
    f = active_proto.field.add()
    f.number = 0
    f.name = 'bar'
    f.oneof_index = 2
    f = active_proto.field.add()
    f.number = 3
    f.name = 'baz'
    active_proto.reserved_name.append('wow')
    rr = active_proto.reserved_range.add()
    rr.start = 2
    rr.end = 3
    active_proto.oneof_decl.add().name = 'ign'
    active_proto.oneof_decl.add().name = 'ign2'
    active_proto.oneof_decl.add().name = 'some_oneof'
    shadow_proto = descriptor_pb2.DescriptorProto()
    f = shadow_proto.field.add()
    f.number = 1
    f.name = 'foo'
    f = shadow_proto.field.add()
    f.number = 0
    f.name = 'bar'
    f = shadow_proto.field.add()
    f.number = 3
    f.name = 'baz'
    f = shadow_proto.field.add()
    f.number = 2
    f.name = 'hidden_envoy_deprecated_wow'
    f.oneof_index = 0
    shadow_proto.oneof_decl.add().name = 'some_oneof'
    target_proto = descriptor_pb2.DescriptorProto()
    merge_active_shadow.MergeActiveShadowMessage(active_proto, shadow_proto, target_proto)
    tf = target_proto.field
    assert len(tf) == 4
    assert tf[2].name == 'bar'
    assert tf[3].name == 'hidden_envoy_deprecated_wow'
    assert tf[3].oneof_index == 2

  def testMergeActiveShadowMessageMissing(self):
    """MergeActiveShadowMessage recovers missing messages from shadow."""
    active_proto = descriptor_pb2.DescriptorProto()
    shadow_proto = descriptor_pb2.DescriptorProto()
    shadow_proto.nested_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    merge_active_shadow.MergeActiveShadowMessage(active_proto, shadow_proto, target_proto)
    assert target_proto.nested_type[0].name == 'foo'

  def testMergeActiveShadowFileMissing(self):
    """MergeActiveShadowFile recovers missing messages from shadow."""
    active_proto = descriptor_pb2.FileDescriptorProto()
    shadow_proto = descriptor_pb2.FileDescriptorProto()
    shadow_proto.message_type.add().name = 'foo'
    target_proto = descriptor_pb2.DescriptorProto()
    target_proto = merge_active_shadow.MergeActiveShadowFile(active_proto, shadow_proto)
    assert target_proto.message_type[0].name == 'foo'


# TODO(htuch): add some test for recursion.

if __name__ == '__main__':
  unittest.main()
