#pragma once

#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/ip_tagging/ip_tagging_filter.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class IpTaggingFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::ip_tagging::v3::IPTagging> {
public:
  IpTaggingFilterFactory() : FactoryBase(HttpFilterNames::get().IpTagging) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

class IpTaggingFileConfig {
public:
  IpTaggingFileConfig(Envoy::Server::Configuration::FactoryContext& factory_context,
                      std::string path);
  std::shared_ptr<const ValueSet> values() const { return watcher_->get(); }

private:
  std::shared_ptr<const ValueSetWatcher> watcher_;
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
