#pragma once

#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.h"
#include "envoy/extensions/filters/network/mtls_failure_response/v3/mtls_failure_response.pb.validate.h"

#include "source/common/common/shared_token_bucket_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MtlsFailureResponse {

class MtlsFailureResponseConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse> {
public:
  MtlsFailureResponseConfigFactory() : FactoryBase(NetworkFilterNames::get().MtlsFailureResponse) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::mtls_failure_response::v3::MtlsFailureResponse&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace MtlsFailureResponse
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
