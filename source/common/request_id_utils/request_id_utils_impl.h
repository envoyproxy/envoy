#pragma once

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
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
   * Return a newly created instance of the default RequestIDUtils implementation.
   */
  static UtilitiesSharedPtr defaultInstance(Envoy::Runtime::RandomGenerator& random);

  /**
   * Read a RequestIDUtils definition from proto and create it.
   */
  static UtilitiesSharedPtr fromProto(
      const envoy::extensions::filters::network::http_connection_manager::v3::RequestIDExtension&
          config,
      Server::Configuration::FactoryContext& context);
};

} // namespace RequestIDUtils
} // namespace Envoy
