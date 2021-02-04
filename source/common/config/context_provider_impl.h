#pragma once

#include "envoy/config/context_provider.h"

#include "common/config/xds_context_params.h"
#include "common/version/api_version.h"

namespace Envoy {
namespace Config {

class ContextProviderImpl : public ContextProvider {
public:
  ContextProviderImpl(const envoy::config::core::v3::Node& node,
                      const Protobuf::RepeatedPtrField<std::string>& node_context_params)
      : node_context_(XdsContextParams::encodeNodeContext(node, node_context_params)) {
    // Add the range of minor versions supported by the client to the context.
    auto& params = *node_context_.mutable_params();
    params["xds.api.oldest_minor_ver"] =
        absl::StrCat(ApiVersionInfo::oldestApiVersion().version().minor_number());
    params["xds.api.latest_minor_ver"] =
        absl::StrCat(ApiVersionInfo::apiVersion().version().minor_number());
  }

  const xds::core::v3::ContextParams& nodeContext() const override { return node_context_; }

private:
  xds::core::v3::ContextParams node_context_;
};

} // namespace Config
} // namespace Envoy
