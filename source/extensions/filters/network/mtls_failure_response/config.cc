#include "source/extensions/filters/network/mtls_failure_response/config.h"

#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.h"
#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/mtls_failure_response/filter.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MtlsFailureResponse {

Network::FilterFactoryCb MtlsFailureResponseConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse&
        proto_config,
    Server::Configuration::FactoryContext& context) {
  const auto& config = proto_config;

  std::shared_ptr<SharedTokenBucketImpl> token_bucket_ = nullptr;
  if (config.failure_mode() == envoy::extensions::filters::network::mtls_failure_response::v3::
                                   MtlsFailureResponse::KEEP_CONNECTION_OPEN &&
      config.has_token_bucket()) {
    token_bucket_ = std::make_shared<SharedTokenBucketImpl>(
        config.token_bucket().max_tokens(), context.serverFactoryContext().timeSource(),
        static_cast<double>(config.token_bucket().tokens_per_fill().value()) /
            absl::ToInt64Seconds(absl::Seconds(config.token_bucket().fill_interval().seconds()) +
                                 absl::Nanoseconds(config.token_bucket().fill_interval().nanos())));
  }

  return [config, &context, token_bucket_](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<MtlsFailureResponseFilter>(config, context, token_bucket_));
  };

};

/**
 * Static registration for the MtlsFailureResponse filter. @see RegisterFactory.
 */
REGISTER_FACTORY(MtlsFailureResponseConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace MtlsFailureResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
