#pragma once

#include "envoy/api/v2/core/config_source.pb.h"

#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

// Translates an xDS resource type_url to the name of the delta gRPC service that carries it.
const Protobuf::MethodDescriptor&
deltaGrpcMethod(absl::string_view resource_type_url,
                envoy::api::v2::core::ApiVersion transport_api_version);
// Translates an xDS resource type_url to the name of the state-of-the-world gRPC service that
// carries it.
const Protobuf::MethodDescriptor&
sotwGrpcMethod(absl::string_view resource_type_url,
               envoy::api::v2::core::ApiVersion transport_api_version);
// Translates an xDS resource type_url to the name of the REST service that carries it.
const Protobuf::MethodDescriptor&
restMethod(absl::string_view resource_type_url,
           envoy::api::v2::core::ApiVersion transport_api_version);

} // namespace Config
} // namespace Envoy
