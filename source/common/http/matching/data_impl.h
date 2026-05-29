#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Http {
namespace Matching {

/**
 * Implementation of HttpMatchingData, providing HTTP specific data to
 * the match tree.
 *
 * This using declaration will be removed, once all references have been migrated to
 * HttpMatchingData.
 */
using HttpMatchingDataImpl = HttpMatchingData;

struct HttpFilterActionContext {
  // Identify whether the filter is in downstream filter chain or upstream filter chain.
  const bool is_downstream_ = true;
  const std::string& stat_prefix_;
  OptRef<Server::Configuration::FactoryContext> factory_context_;
  OptRef<Server::Configuration::UpstreamFactoryContext> upstream_factory_context_;
  OptRef<Server::Configuration::ServerFactoryContext> server_factory_context_;
};

} // namespace Matching
} // namespace Http
} // namespace Envoy
