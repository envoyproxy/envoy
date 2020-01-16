#pragma once

#include "envoy/request_id_utils/request_id_utils.h"
#include "envoy/server/request_id_utils_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace RequestIDUtils {
/**
 * Request ID Utilities factory that reads the configuration from proto.
 */
class RequestIDUtilsFactory {
public:
  /**
   * Our default implementation is the UUID one that matches the preexisting envoy behavior
   */
  static std::string defaultFactoryName();
  /**
   * Build a RequestIDUtils::Utilities instance from the proto config.
   */
  static UtilitiesSharedPtr byName(const std::string&,
                                   Server::Configuration::FactoryContext& context);

  /**
   * Return a newly created instance of the default RequestIDUtils implementation.
   */
  static UtilitiesSharedPtr defaultInstance(Server::Configuration::FactoryContext& context);
};

} // namespace RequestIDUtils
} // namespace Envoy
