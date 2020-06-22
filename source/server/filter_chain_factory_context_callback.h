#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/common/pure.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {

/**
 * Handles FilterChainFactoryContext creation. It is used by a listener to add a new filter chain
 * without worrying about the lifetime of each factory context.
 */
class FilterChainFactoryContextCreator {
public:
  virtual ~FilterChainFactoryContextCreator() = default;

  /**
   * Generate the filter chain factory context from proto. Note the caller does not own the filter
   * chain context.
   */
  virtual Configuration::FilterChainFactoryContextPtr createFilterChainFactoryContext(
      const ::envoy::config::listener::v3::FilterChain* const filter_chain) PURE;
};

} // namespace Server
} // namespace Envoy
