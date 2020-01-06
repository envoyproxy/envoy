#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/common/pure.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {

/**
 * Handles FilterChainFactoryContext creation. It is used by listener to adding new filter chain
 * without worrying the lifetime of each factory context.
 */
class FilterChainFactoryContextCallback {
public:
  virtual ~FilterChainFactoryContextCallback() = default;

  /**
   * Generate the filter chain factory context from proto. Note the callback will own the filter
   * chain context.
   */
  virtual std::shared_ptr<Configuration::FilterChainFactoryContext> createFilterChainFactoryContext(
      const ::envoy::config::listener::v3alpha::FilterChain* const filter_chain) PURE;
};

} // namespace Server
} // namespace Envoy