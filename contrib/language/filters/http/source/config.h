#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/language/v3alpha/language.pb.h"
#include "contrib/envoy/extensions/filters/http/language/v3alpha/language.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Language {

/**
 * Config registration for the language detection filter (i18n). @see NamedHttpFilterConfigFactory.
 */
class LanguageFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::language::v3alpha::Language> {
public:
  LanguageFilterFactory() : FactoryBase("envoy.filters.http.language") {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::language::v3alpha::Language& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(LanguageFilterFactory);

} // namespace Language
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
