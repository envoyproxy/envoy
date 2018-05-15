#pragma once

#include "envoy/config/filter/http/lua/v2/lua.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Lua {

/**
 * Config registration for the Lua filter. @see NamedHttpFilterConfigFactory.
 */
class LuaFilterConfig : public Common::FactoryBase<envoy::config::filter::http::lua::v2::Lua> {
public:
  LuaFilterConfig() : FactoryBase(HttpFilterNames::get().LUA) {}

  Http::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config, const std::string& stats_prefix,
                      Server::Configuration::FactoryContext& context) override;

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::config::filter::http::lua::v2::Lua& proto_config,
                                    const std::string&,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace Lua
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
