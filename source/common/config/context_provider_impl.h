#pragma once

#include "envoy/config/context_provider.h"

#include "common/config/xds_context_params.h"

namespace Envoy {
namespace Config {

class ContextProviderImpl : public ContextProvider {
public:
  ContextProviderImpl(const envoy::config::core::v3::Node& node,
                      const Protobuf::RepeatedPtrField<std::string>& node_context_params)
      : node_context_(XdsContextParams::encodeNodeContext(node, node_context_params)) {}

  const xds::core::v3::ContextParams& nodeContext() const override { return node_context_; }

private:
  const xds::core::v3::ContextParams node_context_;
};

} // namespace Config
} // namespace Envoy
