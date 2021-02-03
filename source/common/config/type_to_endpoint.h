#pragma once

#include "envoy/config/core/v3/config_source.pb.h"

#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

// Translates an xDS resource type_url to the name of the delta gRPC service that carries it.
const Protobuf::MethodDescriptor&
deltaGrpcMethod(absl::string_view resource_type_url,
                envoy::config::core::v3::ApiVersion transport_api_version);
// Translates an xDS resource type_url to the name of the state-of-the-world gRPC service that
// carries it.
const Protobuf::MethodDescriptor&
sotwGrpcMethod(absl::string_view resource_type_url,
               envoy::config::core::v3::ApiVersion transport_api_version);
// Translates an xDS resource type_url to the name of the REST service that carries it.
const Protobuf::MethodDescriptor&
restMethod(absl::string_view resource_type_url,
           envoy::config::core::v3::ApiVersion transport_api_version);

} // namespace Config
} // namespace Envoy
