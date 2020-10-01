#pragma once

#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace LocalInfo {

/**
 * Information about the local environment.
 */
class LocalInfo {
public:
  virtual ~LocalInfo() = default;

  /**
   * @return the local (non-loopback) address of the server.
   */
  virtual Network::Address::InstanceConstSharedPtr address() const PURE;

  /**
   * @return the human readable zone name. E.g., "us-east-1a".
   */
  virtual const std::string& zoneName() const PURE;

  /**
   * @return the human readable cluster name. E.g., "eta".
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * @return the human readable individual node name. E.g., "i-123456".
   */
  virtual const std::string& nodeName() const PURE;

  /**
   * @return the full node identity presented to management servers.
   */
  virtual const envoy::config::core::v3::Node& node() const PURE;
};

using LocalInfoPtr = std::unique_ptr<LocalInfo>;

} // namespace LocalInfo
} // namespace Envoy
