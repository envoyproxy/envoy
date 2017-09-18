#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_field.h"
#include "google/protobuf/service.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/stubs/status.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/json_util.h"
#include "google/protobuf/util/message_differencer.h"
#include "google/protobuf/util/time_util.h"
#include "google/protobuf/util/type_resolver.h"
#include "google/protobuf/util/type_resolver_util.h"
#include "google/protobuf/wrappers.pb.h"

namespace Envoy {

// All references to google::protobuf in Envoy need to be made via the
// Envoy::Protobuf namespace. This is required to allow remapping of protobuf to
// alternative implementations during import into other repositories. E.g. at
// Google we have more than one protobuf implementation.
namespace Protobuf = google::protobuf;

// Allows mapping from google::protobuf::util to other util libraries.
namespace ProtobufUtil = google::protobuf::util;

// Protobuf well-known types (WKT) should be referenced via the ProtobufWkt
// namespace.
namespace ProtobufWkt = google::protobuf;

// Alternative protobuf implementations might not use std::string as a string
// type. Below we provide wrappers to facilitate remapping of the type during
// import.
namespace ProtobufTypes {

typedef std::unique_ptr<Protobuf::Message> MessagePtr;

typedef std::string String;
typedef int64_t Int64;

} // namespace ProtobufTypes
} // namespace Envoy
