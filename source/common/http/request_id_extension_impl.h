#pragma once

#include "envoy/common/random_generator.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/server/request_id_extension_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Http {
/**
 * Request ID Utilities factory that reads the configuration from proto.
 */
class RequestIDExtensionFactory {
public:
  /**
   * Return a newly created instance of the default RequestIDExtension implementation.
   */
  static RequestIDExtensionSharedPtr defaultInstance(Envoy::Random::RandomGenerator& random);

  /**
   * Return a globally shared instance of the noop RequestIDExtension implementation.
   */
  static RequestIDExtensionSharedPtr noopInstance();

  /**
   * Read a RequestIDExtension definition from proto and create it.
   */
  static RequestIDExtensionSharedPtr fromProto(
      const envoy::extensions::filters::network::http_connection_manager::v3::RequestIDExtension&
          config,
      Server::Configuration::FactoryContext& context);
};

} // namespace Http
} // namespace Envoy
