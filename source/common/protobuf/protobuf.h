#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "envoy/common/platform.h"

#include "google/protobuf/any.pb.h"
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

#if defined(ENVOY_ENABLE_FULL_PROTOS)

namespace google::protobuf {
using ReflectableMessage = ::google::protobuf::Message*;
} // namespace google::protobuf

namespace Envoy {
// All references to google::protobuf in Envoy need to be made via the
// Envoy::Protobuf namespace. This is required to allow remapping of protobuf to
// alternative implementations during import into other repositories. E.g. at
// Google we have more than one protobuf implementation.
namespace Protobuf = google::protobuf;

} // namespace Envoy

#else

// Forward declarations
namespace google::protobuf {
class FileDescriptorSet;
} // namespace google::protobuf
namespace cc_proto_descriptor_library::internal {
struct FileDescriptorInfo;
} // namespace cc_proto_descriptor_library::internal

namespace Envoy {
class MessageLiteDifferencer;
// All references to google::protobuf in Envoy need to be made via the
// Envoy::Protobuf namespace. This is required to allow remapping of protobuf to
// alternative implementations during import into other repositories. E.g. at
// Google we have more than one protobuf implementation.
namespace Protobuf {

using Closure = ::google::protobuf::Closure;

using ::google::protobuf::Arena;                        // NOLINT(misc-unused-using-decls)
using ::google::protobuf::BytesValue;                   // NOLINT(misc-unused-using-decls)
using ::google::protobuf::Descriptor;                   // NOLINT(misc-unused-using-decls)
using ::google::protobuf::DescriptorPool;               // NOLINT(misc-unused-using-decls)
using ::google::protobuf::DescriptorPoolDatabase;       // NOLINT(misc-unused-using-decls)
using ::google::protobuf::DynamicCastToGenerated;       // NOLINT(misc-unused-using-decls)
using ::google::protobuf::DynamicMessageFactory;        // NOLINT(misc-unused-using-decls)
using ::google::protobuf::EnumValueDescriptor;          // NOLINT(misc-unused-using-decls)
using ::google::protobuf::FieldDescriptor;              // NOLINT(misc-unused-using-decls)
using ::google::protobuf::FieldMask;                    // NOLINT(misc-unused-using-decls)
using ::google::protobuf::FileDescriptor;               // NOLINT(misc-unused-using-decls)
using ::google::protobuf::FileDescriptorProto;          // NOLINT(misc-unused-using-decls)
using ::google::protobuf::FileDescriptorSet;            // NOLINT(misc-unused-using-decls)
using ::google::protobuf::Map;                          // NOLINT(misc-unused-using-decls)
using ::google::protobuf::MapPair;                      // NOLINT(misc-unused-using-decls)
using ::google::protobuf::MessageFactory;               // NOLINT(misc-unused-using-decls)
using ::google::protobuf::MethodDescriptor;             // NOLINT(misc-unused-using-decls)
using ::google::protobuf::OneofDescriptor;              // NOLINT(misc-unused-using-decls)
using ::google::protobuf::Reflection;                   // NOLINT(misc-unused-using-decls)
using ::google::protobuf::RepeatedField;                // NOLINT(misc-unused-using-decls)
using ::google::protobuf::RepeatedFieldBackInserter;    // NOLINT(misc-unused-using-decls)
using ::google::protobuf::RepeatedPtrField;             // NOLINT(misc-unused-using-decls)
using ::google::protobuf::RepeatedPtrFieldBackInserter; // NOLINT(misc-unused-using-decls)
using ::google::protobuf::TextFormat;                   // NOLINT(misc-unused-using-decls)
using ::google::protobuf::Type;                         // NOLINT(misc-unused-using-decls)
using ::google::protobuf::UInt32Value;                  // NOLINT(misc-unused-using-decls)

using Message = ::google::protobuf::MessageLite;

template <typename T> const T* DynamicCastToGenerated(const Message* from) {
  return static_cast<const T*>(from);
}

template <typename T> T* DynamicCastToGenerated(Message* from) {
  const Message* message_const = from;
  return const_cast<T*>(DynamicCastToGenerated<T>(message_const));
}

using ReflectableMessage = std::unique_ptr<::google::protobuf::Message>;
using uint32 = uint32_t;
using int32 = int32_t;

namespace internal {
using ::google::protobuf::internal::WireFormatLite; // NOLINT(misc-unused-using-decls)
} // namespace internal

namespace io {
using ::google::protobuf::io::ArrayOutputStream;   // NOLINT(misc-unused-using-decls)
using ::google::protobuf::io::CodedInputStream;    // NOLINT(misc-unused-using-decls)
using ::google::protobuf::io::CodedOutputStream;   // NOLINT(misc-unused-using-decls)
using ::google::protobuf::io::IstreamInputStream;  // NOLINT(misc-unused-using-decls)
using ::google::protobuf::io::OstreamOutputStream; // NOLINT(misc-unused-using-decls)
using ::google::protobuf::io::StringOutputStream;  // NOLINT(misc-unused-using-decls)
using ::google::protobuf::io::ZeroCopyInputStream; // NOLINT(misc-unused-using-decls)
} // namespace io

namespace util {
using ::google::protobuf::util::JsonStringToMessage;              // NOLINT(misc-unused-using-decls)
using ::google::protobuf::util::MessageToJsonString;              // NOLINT(misc-unused-using-decls)
using ::google::protobuf::util::NewTypeResolverForDescriptorPool; // NOLINT(misc-unused-using-decls)
using MessageDifferencer = MessageLiteDifferencer;
using ::google::protobuf::util::JsonParseOptions; // NOLINT(misc-unused-using-decls)
using ::google::protobuf::util::JsonPrintOptions; // NOLINT(misc-unused-using-decls)
using ::google::protobuf::util::TimeUtil;         // NOLINT(misc-unused-using-decls)
} // namespace util

} // namespace Protobuf

class MessageLiteDifferencer {
public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool Equals(const Protobuf::Message& message1, const Protobuf::Message& message2);

  // NOLINTNEXTLINE(readability-identifier-naming)
  static bool Equivalent(const Protobuf::Message& message1, const Protobuf::Message& message2);
};

using ConstMessagePtrVector = std::vector<std::unique_ptr<const Protobuf::Message>>;

using FileDescriptorInfo = ::cc_proto_descriptor_library::internal::FileDescriptorInfo;
void loadFileDescriptors(const FileDescriptorInfo& file_descriptor_info);

} // namespace Envoy

#endif

namespace Envoy {

// Allows mapping from google::protobuf::util to other util libraries.
namespace ProtobufUtil = ::google::protobuf::util;

// Protobuf well-known types (WKT) should be referenced via the ProtobufWkt
// namespace.
namespace ProtobufWkt = ::google::protobuf;

// Alternative protobuf implementations might not have the same basic types.
// Below we provide wrappers to facilitate remapping of the type during import.
namespace ProtobufTypes {

using MessagePtr = std::unique_ptr<Protobuf::Message>;
using ConstMessagePtrVector = std::vector<std::unique_ptr<const Protobuf::Message>>;

using Int64 = int64_t;

} // namespace ProtobufTypes

Protobuf::ReflectableMessage createReflectableMessage(const Protobuf::Message& message);

} // namespace Envoy
