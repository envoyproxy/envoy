from frozendict import frozendict

from google.protobuf.descriptor_pb2 import FieldDescriptorProto

FIELD_TYPE_NAMES = frozendict({
    FieldDescriptorProto.TYPE_DOUBLE: 'double',
    FieldDescriptorProto.TYPE_FLOAT: 'float',
    FieldDescriptorProto.TYPE_INT32: 'int32',
    FieldDescriptorProto.TYPE_SFIXED32: 'int32',
    FieldDescriptorProto.TYPE_SINT32: 'int32',
    FieldDescriptorProto.TYPE_FIXED32: 'uint32',
    FieldDescriptorProto.TYPE_UINT32: 'uint32',
    FieldDescriptorProto.TYPE_INT64: 'int64',
    FieldDescriptorProto.TYPE_SFIXED64: 'int64',
    FieldDescriptorProto.TYPE_SINT64: 'int64',
    FieldDescriptorProto.TYPE_FIXED64: 'uint64',
    FieldDescriptorProto.TYPE_UINT64: 'uint64',
    FieldDescriptorProto.TYPE_BOOL: 'bool',
    FieldDescriptorProto.TYPE_STRING: 'string',
    FieldDescriptorProto.TYPE_BYTES: 'bytes'
})

FIELD_LABEL_NAMES = frozendict({
    FieldDescriptorProto.LABEL_OPTIONAL: '',
    FieldDescriptorProto.LABEL_REPEATED: '**repeated** '
})

# Namespace prefix for Envoy core APIs.
ENVOY_API_NAMESPACE_PREFIX = '.envoy.api.v2.'

# Last documented v2 api version
ENVOY_LAST_V2_VERSION = "1.17"

# Namespace prefix for Envoy top-level APIs.
ENVOY_PREFIX = '.envoy.'

# Namespace prefix for WKTs.
WKT_NAMESPACE_PREFIX = '.google.protobuf.'

# Namespace prefix for RPCs.
RPC_NAMESPACE_PREFIX = '.google.rpc.'

# Namespace prefix for cncf/xds top-level APIs.
CNCF_PREFIX = '.xds.'

# http://www.fileformat.info/info/unicode/char/2063/index.htm
UNICODE_INVISIBLE_SEPARATOR = u'\u2063'
