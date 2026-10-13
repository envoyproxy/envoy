#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/client_cert/v3alpha/client_cert.pb.h"
#include "contrib/envoy/extensions/filters/http/client_cert/v3alpha/client_cert.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ClientCert {

/**
 * Config registration for the client_cert filter.
 */
class ClientCertFilterFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::client_cert::v3alpha::ClientCertConfig> {
public:
  ClientCertFilterFactory() : UnifiedFactoryBase("envoy.filters.http.client_cert") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::client_cert::v3alpha::ClientCertConfig& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(ClientCertFilterFactory);

} // namespace ClientCert
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
