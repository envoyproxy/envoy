#include "source/extensions/filters/http/csrf/csrf_filter.h"

#include "envoy/extensions/filters/http/csrf/v3/csrf.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/empty_string.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    origin_handle(Http::CustomHeaders::get().Origin);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    referer_handle(Http::CustomHeaders::get().Referer);

struct RcDetailsValues {
  const std::string OriginMismatch = "csrf_origin_mismatch";
};
using RcDetails = ConstSingleton<RcDetailsValues>;

namespace {
bool isModifyMethod(const Http::RequestHeaderMap& headers) {
  const absl::string_view method_type = headers.getMethodValue();
  if (method_type.empty()) {
    return false;
  }
  const auto& method_values = Http::Headers::get().MethodValues;
  return (method_type == method_values.Post || method_type == method_values.Put ||
          method_type == method_values.Delete || method_type == method_values.Patch);
}

std::string hostAndPort(const absl::string_view absolute_url) {
  Http::Utility::Url url;
  if (!absolute_url.empty()) {
    if (url.initialize(absolute_url, /*is_connect=*/false)) {
      return std::string(url.hostAndPort());
    }
    return std::string(absolute_url);
  }
  return EMPTY_STRING;
}

// Note: per https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Origin,
//       the Origin header must include the scheme (and hostAndPort expects
//       an absolute URL).
std::string sourceOriginValue(const Http::RequestHeaderMap& headers) {
  const auto origin = hostAndPort(headers.getInlineValue(origin_handle.handle()));
  if (!origin.empty()) {
    return origin;
  }
  return hostAndPort(headers.getInlineValue(referer_handle.handle()));
}

std::string targetOriginValue(const Http::RequestHeaderMap& headers) {
  const auto host_value = headers.getHostValue();

  // Don't even bother if there's not Host header.
  if (host_value.empty()) {
    return EMPTY_STRING;
  }

  const auto absolute_url = fmt::format(
      "{}://{}", headers.Scheme() != nullptr ? headers.getSchemeValue() : "http", host_value);
  return hostAndPort(absolute_url);
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
  const auto source_origin = sourceOriginValue(headers);
  if (source_origin.empty()) {
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
  const CsrfPolicy* policy =
      Http::Utility::resolveMostSpecificPerFilterConfig<CsrfPolicy>(callbacks_);
  if (policy != nullptr) {
    policy_ = policy;
  } else {
    policy_ = config_->policy();
  }
}

bool CsrfFilter::isValid(const absl::string_view source_origin, Http::RequestHeaderMap& headers) {
  const auto target_origin = targetOriginValue(headers);
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
