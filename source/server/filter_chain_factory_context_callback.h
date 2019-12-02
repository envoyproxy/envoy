#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/common/pure.h"
#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {

/**
 * @brief Experimental interface for filter chain discovery service.
 *
 */
class FilterChainFactoryContextCallback {
public:
  virtual ~FilterChainFactoryContextCallback() = default;
  virtual void prepareFilterChainFactoryContexts() PURE;
  virtual std::shared_ptr<Configuration::FilterChainFactoryContext> createFilterChainFactoryContext(
      const ::envoy::api::v2::listener::FilterChain* const filter_chain) PURE;
  virtual void commitFilterChainFactoryContexts() PURE;
};

} // namespace Server
} // namespace Envoy