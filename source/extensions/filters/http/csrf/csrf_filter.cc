#include "extensions/filters/http/csrf/csrf_filter.h"

#include "envoy/stats/scope.h"

#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

CsrfFilterConfig::CsrfFilterConfig(const std::string& stats_prefix, Stats::Scope& scope)
    : stats_(generateStats(stats_prefix + "csrf.", scope)) {}

CsrfFilter::CsrfFilter(CsrfFilterConfigSharedPtr config)
    : policies_({{nullptr, nullptr}}), config_(std::move(config)) {}

Http::FilterHeadersStatus CsrfFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  if (decoder_callbacks_->route() == nullptr ||
      decoder_callbacks_->route()->routeEntry() == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  policies_ = {{
      decoder_callbacks_->route()->routeEntry()->csrfPolicy(),
      decoder_callbacks_->route()->routeEntry()->virtualHost().csrfPolicy(),
  }};

  if (!enabled() && !shadowEnabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (!modifyMethod(headers)) {
    return Http::FilterHeadersStatus::Continue;
  }

  bool is_valid = true;
  const auto& sourceOrigin = sourceOriginValue(headers);
  if (sourceOrigin == EMPTY_STRING) {
    is_valid = false;
    config_->stats().missing_source_origin_.inc();
  }

  const auto& targetOrigin = targetOriginValue(headers);
  if (sourceOrigin != targetOrigin) {
    is_valid = false;
    config_->stats().request_invalid_.inc();
  }

  if (is_valid == true) {
    config_->stats().request_valid_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (shadowEnabled() && !enabled()) {
    return Http::FilterHeadersStatus::Continue;
  }

  decoder_callbacks_->sendLocalReply(Http::Code::Forbidden, "Invalid origin", nullptr,
                                     absl::nullopt);
  return Http::FilterHeadersStatus::StopIteration;
}

bool CsrfFilter::modifyMethod(const Http::HeaderMap& headers) {
  const auto& method = headers.Method();
  if (method == nullptr) {
    return false;
  }
  const auto& method_type = method->value().c_str();
  return (method_type == Http::Headers::get().MethodValues.Post ||
          method_type == Http::Headers::get().MethodValues.Put ||
          method_type == Http::Headers::get().MethodValues.Delete);
}

absl::string_view CsrfFilter::sourceOriginValue(const Http::HeaderMap& headers) {
  const auto& origin = hostAndPort(headers.Origin());
  if (origin != EMPTY_STRING) {
    return origin;
  }
  return hostAndPort(headers.Referer());
}

absl::string_view CsrfFilter::targetOriginValue(const Http::HeaderMap& headers) {
  return hostAndPort(headers.Host());
}

absl::string_view CsrfFilter::hostAndPort(const Http::HeaderEntry* header) {
  Http::Utility::Url absolute_url;
  if (header != nullptr && !header->value().empty()) {
    if (absolute_url.initialize(header->value().getStringView())) {
      return absolute_url.host_and_port();
    }
    return header->value().getStringView();
  }
  return EMPTY_STRING;
}

bool CsrfFilter::shadowEnabled() {
  for (const auto policy : policies_) {
    if (policy) {
      return policy->shadowEnabled();
    }
  }
  return false;
}

bool CsrfFilter::enabled() {
  for (const auto policy : policies_) {
    if (policy) {
      return policy->enabled();
    }
  }
  return false;
}

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
