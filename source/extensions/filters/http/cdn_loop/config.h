#pragma once

#include <string>

#include "envoy/extensions/filters/http/cdn_loop/v3/cdn_loop.pb.h"
#include "envoy/extensions/filters/http/cdn_loop/v3/cdn_loop.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {

class CdnLoopFilterFactory : public Common::UnifiedFactoryBase<
                                 envoy::extensions::filters::http::cdn_loop::v3::CdnLoopConfig> {
public:
  CdnLoopFilterFactory() : UnifiedFactoryBase("envoy.filters.http.cdn_loop") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cdn_loop::v3::CdnLoopConfig& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
