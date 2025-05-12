#pragma once

#include <string>

#include "envoy/http/filter.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"

#include "unicode/localematcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Language {

/**
 * All stats for the language detection filter (i18n). @see stats_macros.h
 */
// clang-format off
#define LANGUAGE_FILTER_STATS(COUNTER) \
  COUNTER(header)                      \
  COUNTER(default_language)
// clang-format on

/**
 * Wrapper struct filter stats. @see stats_macros.h
 */
struct LanguageStats {
  LANGUAGE_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Abstract filter configuration.
 */
class LanguageFilterConfig {
public:
  virtual ~LanguageFilterConfig() = default;

  virtual const std::shared_ptr<icu::Locale>& defaultLocale() const PURE;

  virtual const std::shared_ptr<icu::LocaleMatcher>& localeMatcher() const PURE;

  virtual bool clearRouteCache() const PURE;

  virtual LanguageStats& stats() PURE;
};

/**
 * Configuration for the language detection filter (i18n).
 */
class LanguageFilterConfigImpl : public LanguageFilterConfig {
public:
  LanguageFilterConfigImpl(const std::shared_ptr<icu::Locale> default_locale,
                           const std::shared_ptr<icu::LocaleMatcher> locale_matcher,
                           const bool clear_route_cache, const std::string& stats_prefix,
                           Stats::Scope& scope);

  const std::shared_ptr<icu::Locale>& defaultLocale() const override;

  const std::shared_ptr<icu::LocaleMatcher>& localeMatcher() const override;

  bool clearRouteCache() const override;

  LanguageStats& stats() override;

private:
  const std::shared_ptr<icu::Locale> default_locale_;

  const std::shared_ptr<icu::LocaleMatcher> locale_matcher_;

  const bool clear_route_cache_;

  LanguageStats stats_;
};
using LanguageFilterConfigSharedPtr = std::shared_ptr<LanguageFilterConfig>;

class LanguageFilter : public Http::StreamDecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  LanguageFilter(LanguageFilterConfigSharedPtr config);

  static constexpr uint32_t MAX_ACCEPT_LANGUAGE_SIZE = 128;

  static LanguageStats generateStats(const std::string& prefix, Stats::Scope& scope);

  const Http::LowerCaseString Language{"x-language"};
  const Http::LowerCaseString AcceptLanguage{"accept-language"};

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override;

  // Http::StreamFilterBase
  void onDestroy() override;

private:
  LanguageFilterConfigSharedPtr config_;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};

  std::string matchValue(icu::Locale&);
};

} // namespace Language
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
