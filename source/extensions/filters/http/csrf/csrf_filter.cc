#include "extensions/filters/http/csrf/csrf_filter.h"

#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/empty_string.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

struct RcDetailsValues {
  const std::string OriginMismatch = "csrf_origin_mismatch";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

namespace {
bool isModifyMethod(const Http::HeaderMap& headers) {
  const Envoy::Http::HeaderEntry* method = headers.Method();
  if (method == nullptr) {
    return false;
  }
  const absl::string_view method_type = method->value().getStringView();
  const auto& method_values = Http::Headers::get().MethodValues;
  return (method_type == method_values.Post || method_type == method_values.Put ||
          method_type == method_values.Delete || method_type == method_values.Patch);
}

absl::string_view hostAndPort(const Http::HeaderEntry* header) {
  Http::Utility::Url absolute_url;
  if (header != nullptr && !header->value().empty()) {
    if (absolute_url.initialize(header->value().getStringView())) {
      return absolute_url.host_and_port();
    }
    return header->value().getStringView();
  }
  return EMPTY_STRING;
}

absl::string_view sourceOriginValue(const Http::HeaderMap& headers) {
  const absl::string_view origin = hostAndPort(headers.Origin());
  if (origin != EMPTY_STRING) {
    return origin;
  }
  return hostAndPort(headers.Referer());
}

absl::string_view targetOriginValue(const Http::HeaderMap& headers) {
  return hostAndPort(headers.Host());
}

static CsrfStats generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "csrf.";
  return CsrfStats{ALL_CSRF_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

static CsrfPolicyPtr
generatePolicy(const envoy::extensions::filters::http::csrf::v3::CsrfPolicy& policy,
               Runtime::Loader& runtime) {
  return std::make_unique<CsrfPolicy>(policy, runtime);
}
} // namespace

CsrfFilterConfig::CsrfFilterConfig(
    const envoy::extensions::filters::http::csrf::v3::CsrfPolicy& policy,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : stats_(generateStats(stats_prefix, scope)), policy_(generatePolicy(policy, runtime)) {}

CsrfFilter::CsrfFilter(const CsrfFilterConfigSharedPtr config) : config_(config) {}

Http::FilterHeadersStatus CsrfFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  determinePolicy();

  if (!policy_->enabled() && !policy_->shadowEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!isModifyMethod(headers)) {
    return Http::FilterHeadersStatus::Continue;
  }

  bool is_valid = true;
  const absl::string_view source_origin = sourceOriginValue(headers);
  if (source_origin == EMPTY_STRING) {
    is_valid = false;
    config_->stats().missing_source_origin_.inc();
  }

  if (!isValid(source_origin, headers)) {
    is_valid = false;
    config_->stats().request_invalid_.inc();
  }

  if (is_valid == true) {
    config_->stats().request_valid_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (policy_->shadowEnabled() && !policy_->enabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  callbacks_->sendLocalReply(Http::Code::Forbidden, "Invalid origin", nullptr, absl::nullopt,
                             RcDetails::get().OriginMismatch);
  return Http::FilterHeadersStatus::StopIteration;
}

void CsrfFilter::determinePolicy() {
  const std::string& name = Extensions::HttpFilters::HttpFilterNames::get().Csrf;
  const CsrfPolicy* policy =
      Http::Utility::resolveMostSpecificPerFilterConfig<CsrfPolicy>(name, callbacks_->route());
  if (policy != nullptr) {
    policy_ = policy;
  } else {
    policy_ = config_->policy();
  }
}

bool CsrfFilter::isValid(const absl::string_view source_origin, Http::HeaderMap& headers) {
  const absl::string_view target_origin = targetOriginValue(headers);
  if (source_origin == target_origin) {
    return true;
  }

  for (const auto& additional_origin : policy_->additionalOrigins()) {
    if (additional_origin->match(source_origin)) {
      return true;
    }
  }

  return false;
}

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
