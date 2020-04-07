#pragma once

#include <string>

#include "envoy/http/request_id_extension.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implemented for each RequestIDExtension and registered via Registry::registerFactory
 * or the convenience class RegisterFactory.
 */
class RequestIDExtensionFactory : public Envoy::Config::TypedFactory {
public:
  ~RequestIDExtensionFactory() override = default;

  /**
   * Create a Request ID Extension instance from the provided config proto.
   * @param config the custom configuration for this request id extension type.
   * @param context general filter context through which persistent resources can be accessed.
   */
  virtual Http::RequestIDExtensionSharedPtr createExtensionInstance(const Protobuf::Message& config,
                                                                    FactoryContext& context) PURE;

  std::string category() const override { return "envoy.request_id_extension"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
