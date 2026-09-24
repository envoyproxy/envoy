#pragma once

#include <string>
#include <utility>
#include <vector>

#include "envoy/type/ai/v3/endpoint.pb.h"
#include "envoy/type/ai/v3/llm_protocol.pb.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// A well-known service's endpoint layouts: for each protocol it serves, the equivalent template.
struct EndpointPreset {
  std::string name;
  std::vector<std::pair<envoy::type::ai::v3::LLMProtocol, envoy::type::ai::v3::EndpointTemplate>>
      layouts;
  std::vector<std::string> required_variables;
  std::vector<std::string> optional_variables;

  // The layout serving `protocol`, or nullptr when the preset does not serve it.
  const envoy::type::ai::v3::EndpointTemplate*
  layout(envoy::type::ai::v3::LLMProtocol protocol) const;
};

const std::vector<EndpointPreset>& endpointPresets();

// The preset named `name`, or nullptr.
const EndpointPreset* findEndpointPreset(absl::string_view name);

// OK when `endpoint` is a well-formed layout serving `protocol`; otherwise InvalidArgument naming
// the first rule it breaks. Runs after the proto's own validation.
absl::Status validateEndpoint(const envoy::type::ai::v3::Endpoint& endpoint,
                              envoy::type::ai::v3::LLMProtocol protocol);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
