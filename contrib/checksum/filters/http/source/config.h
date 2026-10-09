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
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig> {
public:
  ChecksumFilterFactory() : UnifiedFactoryBase("envoy.filters.http.checksum") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

using UpstreamChecksumFilterFactory = ChecksumFilterFactory;

DECLARE_FACTORY(ChecksumFilterFactory);

} // namespace ChecksumFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
