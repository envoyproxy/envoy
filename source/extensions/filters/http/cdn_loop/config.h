#pragma once

#include <string>

#include "envoy/extensions/filters/http/cdn_loop/v3alpha/cdn_loop.pb.h"
#include "envoy/extensions/filters/http/cdn_loop/v3alpha/cdn_loop.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {

class CdnLoopFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::cdn_loop::v3alpha::CdnLoopConfig> {
public:
  CdnLoopFilterFactory() : FactoryBase(HttpFilterNames::get().CdnLoop) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cdn_loop::v3alpha::CdnLoopConfig& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
