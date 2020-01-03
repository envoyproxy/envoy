#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/common/pure.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {

/**
 * Handles FilterChainFactoryContext creation.
 */
class FilterChainFactoryContextCallback {
public:
  virtual ~FilterChainFactoryContextCallback() = default;

  /**
   * Generate the filter chain factory context from proto. Notes the callback will own the filter
   * chain context.
   */
  virtual std::shared_ptr<Configuration::FilterChainFactoryContext> createFilterChainFactoryContext(
      const ::envoy::api::v2::listener::FilterChain* const filter_chain) PURE;
};

} // namespace Server
} // namespace Envoy