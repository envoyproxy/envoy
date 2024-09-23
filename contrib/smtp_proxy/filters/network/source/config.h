#pragma once

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha/smtp_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha/smtp_proxy.pb.validate.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

/**
 * Config registration for the smtp proxy filter. @see NamedNetworkFilterConfigFactory.
 */

class SmtpConfigFactory : public Common::FactoryBase<
                              envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy> {
public:
  SmtpConfigFactory() : FactoryBase{NetworkFilterNames::get().SmtpProxy} {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;

  // std::vector<AccessLog::InstanceSharedPtr> access_logs_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
