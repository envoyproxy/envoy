#include "common/http/codes.h"

#include <cstdint>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Http {

CodeStatsImpl::CodeStatsImpl(Stats::SymbolTable& symbol_table)
    : symbol_table_(symbol_table),
      canary_(makeStatName("canary")),
      canary_upstream_rq_completed_(makeStatName("canary.upstream_rq_completed")),
      canary_upstream_rq_time_(makeStatName("canary.upstream_rq_time")),
      external_(makeStatName("external")),
      external_rq_time_(makeStatName("external.upstream_rq_time")),
      external_upstream_rq_completed_(makeStatName("external.upstream_rq_completed")),
      external_upstream_rq_time_(makeStatName("external.upstream_rq_time")),
      internal_(makeStatName("internal")),
      internal_rq_time_(makeStatName("internal.upstream_rq_time")),
      internal_upstream_rq_completed_(makeStatName("internal.upstream_rq_completed")),
      internal_upstream_rq_time_(makeStatName("internal.upstream_rq_time")),
      upstream_rq_2xx_(makeStatName("upstream_rq_2xx")),
      upstream_rq_3xx_(makeStatName("upstream_rq_3xx")),
      upstream_rq_4xx_(makeStatName("upstream_rq_4xx")),
      upstream_rq_5xx_(makeStatName("upstream_rq_5xx")),
      upstream_rq_completed_(makeStatName("upstream_rq_completed")),
      upstream_rq_time_(makeStatName("upstream_rq_time")),
      vcluster_(makeStatName("vcluster")),
      vhost_(makeStatName("vhost")),
      zone_(makeStatName("zone")),
      canary_upstream_rq_("canary_upstream_rq_", *this),
      external_upstream_rq_("external_upstream_rq_", *this),
      internal_upstream_rq_("internal_upstream_rq_", *this),
      upstream_rq_("upstream_rq_rq_", *this) {}


CodeStatsImpl::~CodeStatsImpl() {
  for (Stats::StatNameStorage& stat_name_storage : storage_) {
    stat_name_storage.free(symbol_table_);
  }
}

Stats::StatName CodeStatsImpl::makeStatName(absl::string_view name) {
  storage_.push_back(Stats::StatNameStorage(name, symbol_table_));
  return storage_.back().statName();
}

void CodeStatsImpl::chargeBasicResponseStat(Stats::Scope& scope, Stats::StatName prefix,
                                            Code response_code) {
  using Join = Stats::StatNameJoiner;

  // Build a dynamic stat for the response code and increment it.
  scope.counterx(Join(prefix, upstream_rq_completed_).statName()).inc();
  scope.counterx(Join(prefix, upstreamRqGroup(response_code)).statName()).inc();
  scope.counterx(Join(prefix, upstream_rq_.statName(response_code)).statName()).inc();
}

void CodeStatsImpl::chargeBasicResponseStat(Stats::Scope& scope, const std::string& prefix,
                                            Code response_code) {
  Stats::StatNameTempStorage prefix_storage(stripTrailingDot(prefix), symbol_table_);
  return chargeBasicResponseStat(scope, prefix_storage.statName(), response_code);
}

void CodeStatsImpl::chargeResponseStat(const ResponseStatInfo& info) {
  Stats::StatNameTempStorage prefix_storage(stripTrailingDot(info.prefix_), symbol_table_);
  Stats::StatName prefix = prefix_storage.statName();
  Code code = static_cast<Code>(info.response_status_code_);

  chargeBasicResponseStat(info.cluster_scope_, prefix, code);

  using Join = Stats::StatNameJoiner;

  // If the response is from a canary, also create canary stats.
  if (info.upstream_canary_) {
    info.cluster_scope_.counterx(Join(
        prefix, canary_upstream_rq_completed_).statName())
        .inc();
    info.cluster_scope_.counterx(Join(
        {prefix, canary_, upstreamRqGroup(code)}).statName())
        .inc();
    info.cluster_scope_.counterx(Join({prefix, canary_, upstream_rq_.statName(code)}).statName())
        .inc();
  }

  // Split stats into external vs. internal.
  if (info.internal_request_) {
    info.cluster_scope_.counterx(Join(prefix, internal_upstream_rq_completed_).statName()).inc();
    info.cluster_scope_.counterx(Join({prefix, internal_, upstreamRqGroup(code)}).statName())
        .inc();
    info.cluster_scope_.counterx(Join({prefix, internal_, upstream_rq_.statName(code)})
                                 .statName()).inc();
  } else {
    info.cluster_scope_.counterx(Join(prefix, external_upstream_rq_completed_).statName()).inc();
    info.cluster_scope_.counterx(Join({prefix, external_, upstreamRqGroup(code)}).statName()).inc();
    info.cluster_scope_.counterx(Join(prefix, external_, upstream_rq_.statName(code)).statName())
        .inc();
  }

  // Handle request virtual cluster.
  if (!info.request_vcluster_name_.empty()) {
    info.global_scope_
        .counter(Join({vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_,
                       upstream_rq_completed_}))
        .inc();
    info.global_scope_
        .counter(join({vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_,
                       absl::StrCat(upstream_rq_, rc_group)}))
        .inc();
    info.global_scope_
        .counter(join({vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_,
                       absl::StrCat(upstream_rq_, response_code)}))
        .inc();
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    info.cluster_scope_
        .counter(join({prefix_without_trailing_dot, zone_, info.from_zone_, info.to_zone_,
                       upstream_rq_completed_}))
        .inc();
    info.cluster_scope_
        .counter(join({prefix_without_trailing_dot, zone_, info.from_zone_, info.to_zone_,
                       absl::StrCat(upstream_rq_, rc_group)}))
        .inc();
    info.cluster_scope_
        .counter(join({prefix_without_trailing_dot, zone_, info.from_zone_, info.to_zone_,
                       absl::StrCat(upstream_rq_, response_code)}))
        .inc();
  }
}

void CodeStatsImpl::chargeResponseTiming(const ResponseTimingInfo& info) {
  info.cluster_scope_.histogram(absl::StrCat(info.prefix_, upstream_rq_time_))
      .recordValue(info.response_time_.count());
  if (info.upstream_canary_) {
    info.cluster_scope_.histogram(absl::StrCat(info.prefix_, canary_upstream_rq_time_))
        .recordValue(info.response_time_.count());
  }

  if (info.internal_request_) {
    info.cluster_scope_.histogram(absl::StrCat(info.prefix_, internal_upstream_rq_time_))
        .recordValue(info.response_time_.count());
  } else {
    info.cluster_scope_.histogram(absl::StrCat(info.prefix_, external_upstream_rq_time_))
        .recordValue(info.response_time_.count());
  }

  if (!info.request_vcluster_name_.empty()) {
    info.global_scope_
        .histogram(join({vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_,
                         upstream_rq_time_}))
        .recordValue(info.response_time_.count());
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    info.cluster_scope_
        .histogram(join({stripTrailingDot(info.prefix_), zone_, info.from_zone_, info.to_zone_,
                         upstream_rq_time_}))
        .recordValue(info.response_time_.count());
  }
}

absl::string_view CodeStatsImpl::stripTrailingDot(absl::string_view str) {
  if (absl::EndsWith(str, ".")) {
    str.remove_suffix(1);
  }
  return str;
}

std::string CodeStatsImpl::join(const std::vector<absl::string_view>& v) {
  ASSERT(!v.empty());
  auto iter = v.begin();
  if (iter->empty()) {
    ++iter; // Skip any initial empty prefix.
  }
  return absl::StrJoin(iter, v.end(), ".");
}

StatName CodeStatsImpl::upstreamRq(Code response_code) {
  switch (enumToInt(response_code) / 100) {
    case 1: return response_code_1xx_;
    case 2: return response_code_2xx_;
    case 3: return response_code_3xx_;
    case 4: return response_code_3xx_;
    case 5: return response_code_3xx_;
  }
  return StatName();
}

CodeStatsImpl::RequestCodeGroup::~RequestCodeGroup() {
  for (auto p : rc_stat_name_map_) {
    std::unique_ptr<StatNameStorage>& storage = p->second;
    storage->free(code_stats_.symbol_table_);
  }
}

Stats::StatName CodeStatsImpl::RequestCodeGroup::statName(Code response_code) {
  {
    absl::ReaderMutexLock lock(&mutex_);
    auto p = rc_stat_name_map_.find(response_code);
    if (p != rc_stat_name_map_.end()) {
      return p->second;
    }
  }

  // Note -- another thread may swap in here and allocate the lock so we
  // have to re-check after acquiring the write-lock.

  {
    absl::MutexLock lock(&mutex_);
    mutex_.WriterLock();
    std::unique_ptr<StatNameStorage>& stat_name_storage = rc_stat_name_map_[response_code];
    if (stat_name_storage == nullptr) {
      stat_name_storage = std::make_unique<StatNameStorage>(
          absl::StrCat(prefix_, enumToInt(reesponse_code)));
    }
    return stat_name_storage->statName();
  }
}

std::string CodeUtility::groupStringForResponseCode(Code response_code) {
  // Note: this is only used in the unit test and in dynamo_filter.cc, which
  // needs the same sort of symbloziation treatment we are doing here.
  if (CodeUtility::is2xx(enumToInt(response_code))) {
    return "2xx";
  } else if (CodeUtility::is3xx(enumToInt(response_code))) {
    return "3xx";
  } else if (CodeUtility::is4xx(enumToInt(response_code))) {
    return "4xx";
  } else if (CodeUtility::is5xx(enumToInt(response_code))) {
    return "5xx";
  } else {
    return "";
  }
}

const char* CodeUtility::toString(Code code) {
  // clang-format off
  switch (code) {
  // 1xx
  case Code::Continue:                      return "Continue";
  case Code::SwitchingProtocols:            return "Switching Protocols";

  // 2xx
  case Code::OK:                            return "OK";
  case Code::Created:                       return "Created";
  case Code::Accepted:                      return "Accepted";
  case Code::NonAuthoritativeInformation:   return "Non-Authoritative Information";
  case Code::NoContent:                     return "No Content";
  case Code::ResetContent:                  return "Reset Content";
  case Code::PartialContent:                return "Partial Content";
  case Code::MultiStatus:                   return "Multi-Status";
  case Code::AlreadyReported:               return "Already Reported";
  case Code::IMUsed:                        return "IM Used";

  // 3xx
  case Code::MultipleChoices:               return "Multiple Choices";
  case Code::MovedPermanently:              return "Moved Permanently";
  case Code::Found:                         return "Found";
  case Code::SeeOther:                      return "See Other";
  case Code::NotModified:                   return "Not Modified";
  case Code::UseProxy:                      return "Use Proxy";
  case Code::TemporaryRedirect:             return "Temporary Redirect";
  case Code::PermanentRedirect:             return "Permanent Redirect";

  // 4xx
  case Code::BadRequest:                    return "Bad Request";
  case Code::Unauthorized:                  return "Unauthorized";
  case Code::PaymentRequired:               return "Payment Required";
  case Code::Forbidden:                     return "Forbidden";
  case Code::NotFound:                      return "Not Found";
  case Code::MethodNotAllowed:              return "Method Not Allowed";
  case Code::NotAcceptable:                 return "Not Acceptable";
  case Code::ProxyAuthenticationRequired:   return "Proxy Authentication Required";
  case Code::RequestTimeout:                return "Request Timeout";
  case Code::Conflict:                      return "Conflict";
  case Code::Gone:                          return "Gone";
  case Code::LengthRequired:                return "Length Required";
  case Code::PreconditionFailed:            return "Precondition Failed";
  case Code::PayloadTooLarge:               return "Payload Too Large";
  case Code::URITooLong:                    return "URI Too Long";
  case Code::UnsupportedMediaType:          return "Unsupported Media Type";
  case Code::RangeNotSatisfiable:           return "Range Not Satisfiable";
  case Code::ExpectationFailed:             return "Expectation Failed";
  case Code::MisdirectedRequest:            return "Misdirected Request";
  case Code::UnprocessableEntity:           return "Unprocessable Entity";
  case Code::Locked:                        return "Locked";
  case Code::FailedDependency:              return "Failed Dependency";
  case Code::UpgradeRequired:               return "Upgrade Required";
  case Code::PreconditionRequired:          return "Precondition Required";
  case Code::TooManyRequests:               return "Too Many Requests";
  case Code::RequestHeaderFieldsTooLarge:   return "Request Header Fields Too Large";

  // 5xx
  case Code::InternalServerError:           return "Internal Server Error";
  case Code::NotImplemented:                return "Not Implemented";
  case Code::BadGateway:                    return "Bad Gateway";
  case Code::ServiceUnavailable:            return "Service Unavailable";
  case Code::GatewayTimeout:                return "Gateway Timeout";
  case Code::HTTPVersionNotSupported:       return "HTTP Version Not Supported";
  case Code::VariantAlsoNegotiates:         return "Variant Also Negotiates";
  case Code::InsufficientStorage:           return "Insufficient Storage";
  case Code::LoopDetected:                  return "Loop Detected";
  case Code::NotExtended:                   return "Not Extended";
  case Code::NetworkAuthenticationRequired: return "Network Authentication Required";
  }
  // clang-format on

  return "Unknown";
}

} // namespace Http
} // namespace Envoy
