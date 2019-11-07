#pragma once

#include "envoy/api/v2/core/config_source.pb.h"

#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

// Translates an xDS resource type_url and xDS protocol type to the MethodDescriptor
// of the REST or gRPC service that carries it.
const Protobuf::MethodDescriptor&
xdsCarrierMethod(absl::string_view type_url,
                 envoy::api::v2::core::ApiConfigSource::ApiType api_type);

} // namespace Config
} // namespace Envoy
