#pragma once

#include "envoy/common/pure.h"

#include "xds/core/v3/context_params.pb.h"

namespace Envoy {
namespace Config {

/**
 * A provider for xDS context parameters. These are currently derived from the bootstrap, but will
 * be set dynamically at runtime in the near future as we add support for dynamic context parameter
 * discovery and updates.
 */
class ContextProvider {
public:
  virtual ~ContextProvider() = default;

  /**
   * @return const xds::core::v3::ContextParams& node-level context parameters.
   */
  virtual const xds::core::v3::ContextParams& nodeContext() const PURE;
};

} // namespace Config
} // namespace Envoy
