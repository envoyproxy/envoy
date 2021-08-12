#pragma once

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "xds/core/v3/context_params.pb.h"

namespace Envoy {
namespace Config {

// Utilities for working with context parameters.
class XdsContextParams {
public:
  /**
   * Encode resource context parameters by following the xDS transport precedence algorithm and
   * applying parameter prefixes.
   *
   * @param node_context_params node context parameters.
   * @param resource_context_params context parameters from resource locator.
   * @param client_features client feature capabilities.
   * @param extra_resource_param per-resource type well known attributes.
   * @return xds::core::v3::ContextParams encoded context parameters.
   */
  static xds::core::v3::ContextParams
  encodeResource(const xds::core::v3::ContextParams& node_context_params,
                 const xds::core::v3::ContextParams& resource_context_params,
                 const std::vector<std::string>& client_features,
                 const absl::flat_hash_map<std::string, std::string>& extra_resource_params);

  /**
   * Encode node context parameters.
   *
   * @param node reference to the local Node information.
   * @param node_context_params a list of node fields to include in context parameters.
   *
   * @return xds::core::v3::ContextParams encoded node context parameters.
   */
  static xds::core::v3::ContextParams
  encodeNodeContext(const envoy::config::core::v3::Node& node,
                    const Protobuf::RepeatedPtrField<std::string>& node_context_params);
};

} // namespace Config
} // namespace Envoy
