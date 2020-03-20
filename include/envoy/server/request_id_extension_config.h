#pragma once

#include <string>

#include "envoy/request_id_extension/request_id_extension.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implemented for each RequestIDExtension::Utilities and registered via Registry::registerFactory
 * or the convenience class RegisterFactory.
 */
class RequestIDExtensionFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~RequestIDExtensionFactory() = default;

  /**
   * Create a Request ID Utilities instance from the provided config proto.
   * @param config the custom configuration for this request id utilities type.
   * @param context general filter context through which persistent resources can be accessed.
   */
  virtual RequestIDExtension::UtilitiesSharedPtr
  createUtilitiesInstance(const Protobuf::Message& config, FactoryContext& context) PURE;

  std::string category() const override { return "envoy.request_id_extension"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
