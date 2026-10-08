#pragma once

#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {

/**
 * Config registration for the decompressor filter. @see NamedHttpFilterConfigFactory.
 */
class DecompressorFilterFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::decompressor::v3::Decompressor> {
public:
  DecompressorFilterFactory() : UnifiedFactoryBase("envoy.filters.http.decompressor") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::decompressor::v3::Decompressor& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(DecompressorFilterFactory);

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
