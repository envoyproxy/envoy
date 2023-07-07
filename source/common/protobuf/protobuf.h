#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "envoy/common/platform.h"

#include "google/protobuf/any.pb.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
//#include "google/protobuf/json/json.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_field.h"
#include "google/protobuf/service.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/map.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_field.h"
#include "google/protobuf/service.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/field_mask_util.h"
#include "google/protobuf/util/json_util.h"
#include "google/protobuf/util/message_differencer.h"
#include "google/protobuf/util/time_util.h"
#include "google/protobuf/util/type_resolver.h"
#include "google/protobuf/util/type_resolver_util.h"
#include "google/protobuf/wrappers.pb.h"

namespace google::protobuf {
class FileDescriptorSet;
}

namespace Envoy {

class MessageLiteDifferencer;

// All references to google::protobuf in Envoy need to be made via the
// Envoy::Protobuf namespace. This is required to allow remapping of protobuf to
// alternative implementations during import into other repositories. E.g. at
// Google we have more than one protobuf implementation.

namespace Protobuf {
typedef ::google::protobuf::Closure Closure;

using ::google::protobuf::BytesValue;
using ::google::protobuf::Descriptor;
using ::google::protobuf::DescriptorPool;
using ::google::protobuf::DynamicCastToGenerated;
using ::google::protobuf::DynamicMessageFactory;
using ::google::protobuf::EnumValueDescriptor;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::UInt32Value;
using ::google::protobuf::Map;
#ifdef ENVOY_ENABLE_FULL_PROTOS
using ::google::protobuf::Message;
using ReflectableMessage = ::google::protobuf::Message*;
#else
using Message = ::google::protobuf::MessageLite;

template <typename T>
const T* DynamicCastToGenerated(const Message* from) {
  return static_cast<const T*>(from);
}

template <typename T>
T* DynamicCastToGenerated(Message* from) {
  const Message* message_const = from;
  return const_cast<T*>(DynamicCastToGenerated<T>(message_const));
}


using ReflectableMessage = std::unique_ptr<::google::protobuf::Message>;
using uint32 = uint32_t;
#endif
using ::google::protobuf::DescriptorPoolDatabase;
using ::google::protobuf::FileDescriptorProto;
using ::google::protobuf::FileDescriptorSet;
using ::google::protobuf::MessageFactory;
using ::google::protobuf::MethodDescriptor;
using ::google::protobuf::Reflection;
using ::google::protobuf::RepeatedPtrField;
using ::google::protobuf::RepeatedPtrFieldBackInserter;
using ::google::protobuf::RepeatedFieldBackInserter;
using ::google::protobuf::TextFormat;

namespace internal {
using ::google::protobuf::internal::WireFormatLite;
}  // namespace internal

namespace io {
using ::google::protobuf::io::ZeroCopyInputStream;
using ::google::protobuf::io::ArrayOutputStream;
using ::google::protobuf::io::CodedOutputStream;
using ::google::protobuf::io::CodedInputStream;
using ::google::protobuf::io::StringOutputStream;
}  // namespace io

namespace util {
using ::google::protobuf::util::MessageToJsonString;
using ::google::protobuf::util::JsonStringToMessage;
using ::google::protobuf::util::NewTypeResolverForDescriptorPool;
#ifdef ENVOY_ENABLE_FULL_PROTOS
using ::google::protobuf::util::MessageDifferencer;
#else
using MessageDifferencer = MessageLiteDifferencer;
#endif
using ::google::protobuf::util::JsonPrintOptions;
using ::google::protobuf::util::Status;
using ::google::protobuf::util::TimeUtil;
using ::google::protobuf::util::JsonParseOptions;
}  // namespace util

}  // namespace Protobuf

// Allows mapping from google::protobuf::util to other util libraries.
namespace ProtobufUtil = google::protobuf::util;

// Protobuf well-known types (WKT) should be referenced via the ProtobufWkt
// namespace.
namespace ProtobufWkt = google::protobuf;

// Alternative protobuf implementations might not have the same basic types.
// Below we provide wrappers to facilitate remapping of the type during import.
namespace ProtobufTypes {

using MessagePtr = std::unique_ptr<Protobuf::Message>;
using ConstMessagePtrVector = std::vector<std::unique_ptr<const Protobuf::Message>>;

using Int64 = int64_t;

} // namespace ProtobufTypes

Protobuf::ReflectableMessage CreateReflectableMessage(const Protobuf::Message& message);

class MessageLiteDifferencer {
 public:
  static bool Equals(const Protobuf::Message& message1, const Protobuf::Message& message2) {
    return message1.SerializeAsString() == message2.SerializeAsString();
  }

  static bool Equivalent(const Protobuf::Message& message1, const Protobuf::Message& message2) {
    return Equals(message1, message2);
  }
};

typedef std::vector<std::unique_ptr<const Protobuf::Message>> ConstMessagePtrVector;

} // namespace Envoy
