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
    : stat_name_pool_(symbol_table), symbol_table_(symbol_table),
      canary_(stat_name_pool_.add("canary")),
      canary_upstream_rq_time_(stat_name_pool_.add("canary.upstream_rq_time")),
      external_(stat_name_pool_.add("external")),
      external_upstream_rq_time_(stat_name_pool_.add("external.upstream_rq_time")),
      internal_(stat_name_pool_.add("internal")),
      internal_upstream_rq_time_(stat_name_pool_.add("internal.upstream_rq_time")),
      upstream_rq_1xx_(stat_name_pool_.add("upstream_rq_1xx")),
      upstream_rq_2xx_(stat_name_pool_.add("upstream_rq_2xx")),
      upstream_rq_3xx_(stat_name_pool_.add("upstream_rq_3xx")),
      upstream_rq_4xx_(stat_name_pool_.add("upstream_rq_4xx")),
      upstream_rq_5xx_(stat_name_pool_.add("upstream_rq_5xx")),
      upstream_rq_unknown_(stat_name_pool_.add("upstream_rq_unknown")), // Covers invalid http
                                                                        // response codes e.g. 600.
      upstream_rq_completed_(stat_name_pool_.add("upstream_rq_completed")),
      upstream_rq_time_(stat_name_pool_.add("upstream_rq_time")),
      vcluster_(stat_name_pool_.add("vcluster")), vhost_(stat_name_pool_.add("vhost")),
      zone_(stat_name_pool_.add("zone")) {

  for (uint32_t i = 0; i < NumHttpCodes; ++i) {
    rc_stat_names_[i] = nullptr;
  }

  // Pre-allocate response codes 200, 404, and 503, as those seem quite likely.
  // We don't pre-allocate all the HTTP codes because the first 127 allocations
  // are likely to be encoded in one byte, and we would rather spend those on
  // common components of stat-names that appear frequently.
  upstreamRqStatName(Code::OK);
  upstreamRqStatName(Code::NotFound);
  upstreamRqStatName(Code::ServiceUnavailable);
}

void CodeStatsImpl::incCounter(Stats::Scope& scope,
                               const std::vector<Stats::StatName>& names) const {
  Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
  scope.counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
}

void CodeStatsImpl::incCounter(Stats::Scope& scope, Stats::StatName a, Stats::StatName b) const {
  Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join({a, b});
  scope.counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
}

void CodeStatsImpl::recordHistogram(Stats::Scope& scope, const std::vector<Stats::StatName>& names,
                                    uint64_t count) const {
  Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
  scope.histogramFromStatName(Stats::StatName(stat_name_storage.get())).recordValue(count);
}

void CodeStatsImpl::chargeBasicResponseStat(Stats::Scope& scope, Stats::StatName prefix,
                                            Code response_code) const {
  ASSERT(&symbol_table_ == &scope.symbolTable());

  // Build a dynamic stat for the response code and increment it.
  incCounter(scope, prefix, upstream_rq_completed_);
  incCounter(scope, prefix, upstreamRqGroup(response_code));
  incCounter(scope, prefix, upstreamRqStatName(response_code));
}

void CodeStatsImpl::chargeResponseStat(const ResponseStatInfo& info) const {
  Code code = static_cast<Code>(info.response_status_code_);

  ASSERT(&info.cluster_scope_.symbolTable() == &symbol_table_);
  chargeBasicResponseStat(info.cluster_scope_, info.prefix_, code);

  Stats::StatName rq_group = upstreamRqGroup(code);
  Stats::StatName rq_code = upstreamRqStatName(code);

  auto write_category = [this, rq_group, rq_code, &info](Stats::StatName category) {
    incCounter(info.cluster_scope_, {info.prefix_, category, upstream_rq_completed_});
    incCounter(info.cluster_scope_, {info.prefix_, category, rq_group});
    incCounter(info.cluster_scope_, {info.prefix_, category, rq_code});
  };

  // If the response is from a canary, also create canary stats.
  if (info.upstream_canary_) {
    write_category(canary_);
  }

  // Split stats into external vs. internal.
  if (info.internal_request_) {
    write_category(internal_);
  } else {
    write_category(external_);
  }

  // Handle request virtual cluster.
  if (!info.request_vcluster_name_.empty()) {
    incCounter(info.global_scope_, {vhost_, info.request_vhost_name_, vcluster_,
                                    info.request_vcluster_name_, upstream_rq_completed_});
    incCounter(info.global_scope_, {vhost_, info.request_vhost_name_, vcluster_,
                                    info.request_vcluster_name_, rq_group});
    incCounter(info.global_scope_,
               {vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_, rq_code});
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    incCounter(info.cluster_scope_,
               {info.prefix_, zone_, info.from_zone_, info.to_zone_, upstream_rq_completed_});
    incCounter(info.cluster_scope_,
               {info.prefix_, zone_, info.from_zone_, info.to_zone_, rq_group});
    incCounter(info.cluster_scope_, {info.prefix_, zone_, info.from_zone_, info.to_zone_, rq_code});
  }
}

void CodeStatsImpl::chargeResponseTiming(const ResponseTimingInfo& info) const {
  const uint64_t count = info.response_time_.count();
  recordHistogram(info.cluster_scope_, {info.prefix_, upstream_rq_time_}, count);
  if (info.upstream_canary_) {
    recordHistogram(info.cluster_scope_, {info.prefix_, canary_upstream_rq_time_}, count);
  }

  if (info.internal_request_) {
    recordHistogram(info.cluster_scope_, {info.prefix_, internal_upstream_rq_time_}, count);
  } else {
    recordHistogram(info.cluster_scope_, {info.prefix_, external_upstream_rq_time_}, count);
  }

  if (!info.request_vcluster_name_.empty()) {
    recordHistogram(info.global_scope_,
                    {vhost_, info.request_vhost_name_, vcluster_, info.request_vcluster_name_,
                     upstream_rq_time_},
                    count);
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    recordHistogram(info.cluster_scope_,
                    {info.prefix_, zone_, info.from_zone_, info.to_zone_, upstream_rq_time_},
                    count);
  }
}

absl::string_view CodeStatsImpl::stripTrailingDot(absl::string_view str) {
  if (absl::EndsWith(str, ".")) {
    str.remove_suffix(1);
  }
  return str;
}

Stats::StatName CodeStatsImpl::upstreamRqGroup(Code response_code) const {
  switch (enumToInt(response_code) / 100) {
  case 1:
    return upstream_rq_1xx_;
  case 2:
    return upstream_rq_2xx_;
  case 3:
    return upstream_rq_3xx_;
  case 4:
    return upstream_rq_4xx_;
  case 5:
    return upstream_rq_5xx_;
  }
  return upstream_rq_unknown_;
}

Stats::StatName CodeStatsImpl::upstreamRqStatName(Code response_code) const {
  // Take a lock only if we've never seen this response-code before.
  uint32_t rc_int = static_cast<uint32_t>(response_code);
  if (rc_int >= NumHttpCodes) {
    return upstream_rq_unknown_;
  }
  std::atomic<uint8_t*>& atomic_ref = rc_stat_names_[rc_int];
  if (atomic_ref.load() == nullptr) {
    absl::MutexLock lock(&mutex_);

    // Check again under lock as two threads might have raced to add a StatName
    // for the same code.
    if (atomic_ref.load() == nullptr) {
      atomic_ref = stat_name_pool_.addReturningStorage(
          absl::StrCat("upstream_rq_", enumToInt(response_code)));
    }
  }
  return Stats::StatName(atomic_ref.load());
}

std::string CodeUtility::groupStringForResponseCode(Code response_code) {
  // Note: this is only used in the unit test and in dynamo_filter.cc, which
  // needs the same sort of symbolization treatment we are doing here.
  if (CodeUtility::is1xx(enumToInt(response_code))) {
    return "1xx";
  } else if (CodeUtility::is2xx(enumToInt(response_code))) {
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
