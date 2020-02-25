#pragma once

#include <string>

#include "envoy/request_id_utils/request_id_utils.h"
#include "envoy/server/filter_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Implemented for each RequestIDUtils::Utilities and registered via Registry::registerFactory or
 * the convenience class RegisterFactory.
 */
class RequestIDUtilsFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~RequestIDUtilsFactory() = default;

  /**
   * Create a Request ID Utilities instance from the provided config proto.
   * @param config the custom configuration for this request id utilities type.
   * @param context general filter context through which persistent resources can be accessed.
   */
  virtual RequestIDUtils::UtilitiesSharedPtr
  createUtilitiesInstance(const Protobuf::Message& config, FactoryContext& context) PURE;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
