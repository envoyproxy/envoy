#include "extensions/tracers/skywalking/skywalking_client_config.h"

#include "common/config/datasource.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

constexpr uint32_t DEFAULT_DELAYED_SEGMENTS_CACHE_SIZE = 1024;

// When the user does not provide any available configuration, in order to ensure that the service
// name and instance name are not empty, use this value as the default identifier. In practice,
// user should provide accurate configuration as much as possible to avoid using the default value.
constexpr absl::string_view DEFAULT_SERVICE_AND_INSTANCE = "EnvoyProxy";

SkyWalkingClientConfig::SkyWalkingClientConfig(Server::Configuration::TracerFactoryContext& context,
                                               const envoy::config::trace::v3::ClientConfig& config)
    : factory_context_(context.serverFactoryContext()),
      max_cache_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_cache_size,
                                                      DEFAULT_DELAYED_SEGMENTS_CACHE_SIZE)),
      service_(config.service_name().empty() ? factory_context_.localInfo().clusterName().empty()
                                                   ? DEFAULT_SERVICE_AND_INSTANCE
                                                   : factory_context_.localInfo().clusterName()
                                             : config.service_name()),
      instance_(config.instance_name().empty() ? factory_context_.localInfo().nodeName().empty()
                                                     ? DEFAULT_SERVICE_AND_INSTANCE
                                                     : factory_context_.localInfo().nodeName()
                                               : config.instance_name()) {
  // Since the SDS API to get backend token is not supported yet, we can get the value of token
  // from the backend_token field directly. If the user does not provide the configuration, the
  // value of token is kept empty.
  backend_token_ = config.backend_token();
}

// TODO(wbpcode): currently, backend authentication token can only be configured with inline string.
// It will be possible to get authentication through the SDS API later.
const std::string& SkyWalkingClientConfig::backendToken() const { return backend_token_; }

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
