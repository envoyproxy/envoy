#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/checksum/v3alpha/checksum.pb.h"
#include "contrib/envoy/extensions/filters/http/checksum/v3alpha/checksum.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ChecksumFilter {

/**
 * Config registration for the checksum filter.
 */
class ChecksumFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig> {
public:
  ChecksumFilterFactory() : FactoryBase("envoy.filters.http.checksum") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

using UpstreamChecksumFilterFactory = ChecksumFilterFactory;

DECLARE_FACTORY(ChecksumFilterFactory);

} // namespace ChecksumFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
