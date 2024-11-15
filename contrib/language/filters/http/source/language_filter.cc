#include "contrib/language/filters/http/source/language_filter.h"

#include <string>

#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/http/utility.h"

#include "absl/strings/ascii.h"
#include "unicode/locid.h"
#include "unicode/utypes.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Language {

// Validates a string for Accept-Language header value compliance.
// See https://httpwg.org/specs/rfc7231.html#header.accept-language
bool validateHeaderValue(absl::string_view value_str) {
  for (auto ch : value_str) {
    if (!(absl::ascii_isalnum(ch) || ch == '_' || ch == '-' || ch == '=' || ch == '.' ||
          ch == ',' || ch == ';' || ch == '*' || ch == ' ')) {
      return false;
    }
  }

  return true;
}

LanguageFilterConfigImpl::LanguageFilterConfigImpl(
    const std::shared_ptr<icu::Locale> default_locale,
    const std::shared_ptr<icu::LocaleMatcher> locale_matcher, const bool clear_route_cache,
    const std::string& stats_prefix, Stats::Scope& scope)
    : default_locale_(std::move(default_locale)), locale_matcher_(std::move(locale_matcher)),
      clear_route_cache_(clear_route_cache),
      stats_(LanguageFilter::generateStats(stats_prefix, scope)) {}

const std::shared_ptr<icu::Locale>& LanguageFilterConfigImpl::defaultLocale() const {
  return default_locale_;
}

const std::shared_ptr<icu::LocaleMatcher>& LanguageFilterConfigImpl::localeMatcher() const {
  return locale_matcher_;
}

bool LanguageFilterConfigImpl::clearRouteCache() const { return clear_route_cache_; }

LanguageStats& LanguageFilterConfigImpl::stats() { return stats_; }

LanguageFilter::LanguageFilter(const LanguageFilterConfigSharedPtr config)
    : config_(std::move(config)) {}

void LanguageFilter::onDestroy() {}

LanguageStats LanguageFilter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "language.";
  return {LANGUAGE_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

Http::FilterHeadersStatus LanguageFilter::decodeHeaders(Http::RequestHeaderMap& request_headers,
                                                        bool) {
  const auto header = request_headers.get(AcceptLanguage);
  if (!header.empty()) {
    const absl::string_view value_str = header[0]->value().getStringView();

    if (!value_str.empty() && value_str.length() <= MAX_ACCEPT_LANGUAGE_SIZE &&
        validateHeaderValue(value_str)) {
      const std::string value = std::string(value_str);

      UErrorCode errorCode = U_ZERO_ERROR;
      const icu::Locale* result =
          config_->localeMatcher()->getBestMatchForListString(value, errorCode);

      if (U_SUCCESS(errorCode)) {
        const std::string language_tag = result->toLanguageTag<std::string>(errorCode);

        if (U_SUCCESS(errorCode)) {
          if (!language_tag.empty()) {
            request_headers.addCopy(Language, language_tag);

            if (config_->clearRouteCache()) {
              ENVOY_LOG(debug, "clearing route cache");
              decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
            }

            config_->stats().header_.inc();

            return Http::FilterHeadersStatus::Continue;
          }
        }
      }
    }
  }

  // Default language fallback
  request_headers.addCopy(Language, config_->defaultLocale()->getLanguage());
  config_->stats().default_language_.inc();

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus LanguageFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus LanguageFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void LanguageFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

std::string LanguageFilter::matchValue(icu::Locale& locale) {
  UErrorCode errorCode = U_ZERO_ERROR;
  const icu::Locale* result = config_->localeMatcher()->getBestMatch(locale, errorCode);

  if (U_SUCCESS(errorCode)) {
    const auto language_tag = result->toLanguageTag<std::string>(errorCode);

    if (U_SUCCESS(errorCode)) {
      return language_tag;
    } else {
      ENVOY_LOG(error, "ICU error code icu::LocaleMatcher toLanguageTag: {}",
                static_cast<int>(errorCode));
    }
  } else {
    ENVOY_LOG(error, "ICU error code icu::LocaleMatcher getBestMatch: {}",
              static_cast<int>(errorCode));
  }

  return EMPTY_STRING;
}

} // namespace Language
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
