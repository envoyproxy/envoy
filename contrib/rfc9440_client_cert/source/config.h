#pragma once

#include "envoy/extensions/filters/http/rfc9440_client_cert/v3/rfc9440_client_cert.pb.h"
#include "envoy/extensions/filters/http/rfc9440_client_cert/v3/rfc9440_client_cert.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Rfc9440ClientCert {

class Rfc9440ClientCertFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::rfc9440_client_cert::v3::Rfc9440ClientCert> {
public:
  Rfc9440ClientCertFilterFactory() : FactoryBase("envoy.filters.http.rfc9440_client_cert") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::rfc9440_client_cert::v3::Rfc9440ClientCert&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Rfc9440ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
