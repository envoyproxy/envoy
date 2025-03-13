#include "contrib/language/filters/http/source/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "contrib/language/filters/http/source/language_filter.h"
#include "unicode/localematcher.h"
#include "unicode/utypes.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Language {

struct LocaleHash {
  size_t operator()(icu::Locale const value) const {
    return absl::Hash<int32_t>{}(value.hashCode());
  }
};

Http::FilterFactoryCb LanguageFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::language::v3alpha::Language& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  const auto default_locale = icu::Locale(proto_config.default_language().data());

  if (default_locale.isBogus()) {
    throw EnvoyException(fmt::format("Failed to create icu::Locale from default_language: {}",
                                     proto_config.default_language().data()));
  }

  absl::flat_hash_set<icu::Locale, LocaleHash> supported_languages({default_locale});

  for (const auto& supported_language : proto_config.supported_languages()) {
    const auto locale = icu::Locale(supported_language.data());

    if (locale.isBogus()) {
      throw EnvoyException(fmt::format("Failed to create icu::Locale from supported_languages: {}",
                                       supported_language.data()));
    }

    supported_languages.insert(locale);
  }

  UErrorCode errorCode = U_ZERO_ERROR;
  const auto locale_matcher = std::make_shared<icu::LocaleMatcher>(
      icu::LocaleMatcher::Builder()
          .setSupportedLocales(supported_languages.begin(), supported_languages.end())
          .setDefaultLocale(&default_locale)
          .build(errorCode));

  if (U_FAILURE(errorCode)) {
    throw EnvoyException(fmt::format("Failed to initialize icu::LocaleMatcher::Builder: ICU error "
                                     "code icu::LocaleMatcher::Builder build: {}",
                                     static_cast<int>(errorCode)));
  }

  auto config = std::make_shared<LanguageFilterConfigImpl>(
      std::make_shared<icu::Locale>(default_locale), locale_matcher,
      proto_config.clear_route_cache(), stats_prefix, context.scope());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<LanguageFilter>(config);
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{filter});
  };
}

/**
 * Static registration for the language detection filter (i18n). @see RegisterFactory.
 */
REGISTER_FACTORY(LanguageFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Language
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
