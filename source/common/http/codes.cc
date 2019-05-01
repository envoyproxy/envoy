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
    : storage_(symbol_table), symbol_table_(symbol_table), canary_(makeStatName("canary")),
      canary_upstream_rq_time_(makeStatName("canary.upstream_rq_time")),
      external_(makeStatName("external")),
      external_rq_time_(makeStatName("external.upstream_rq_time")),
      external_upstream_rq_time_(makeStatName("external.upstream_rq_time")),
      internal_(makeStatName("internal")),
      internal_rq_time_(makeStatName("internal.upstream_rq_time")),
      internal_upstream_rq_time_(makeStatName("internal.upstream_rq_time")),
      upstream_rq_1xx_(makeStatName("upstream_rq_1xx")),
      upstream_rq_2xx_(makeStatName("upstream_rq_2xx")),
      upstream_rq_3xx_(makeStatName("upstream_rq_3xx")),
      upstream_rq_4xx_(makeStatName("upstream_rq_4xx")),
      upstream_rq_5xx_(makeStatName("upstream_rq_5xx")),
      upstream_rq_unknown_(makeStatName("upstream_rq_Unknown")),
      upstream_rq_completed_(makeStatName("upstream_rq_completed")),
      upstream_rq_time_(makeStatName("upstream_rq_time")), vcluster_(makeStatName("vcluster")),
      vhost_(makeStatName("vhost")), zone_(makeStatName("zone")),
      canary_upstream_rq_("canary_upstream_rq_", *this),
      external_upstream_rq_("external_upstream_rq_", *this),
      internal_upstream_rq_("internal_upstream_rq_", *this), upstream_rq_("upstream_rq_", *this) {}

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
  incCounter(scope, prefix, upstream_rq_.statName(response_code));
}

void CodeStatsImpl::chargeResponseStat(const ResponseStatInfo& info) const {
  Stats::StatNameManagedContainer stat_name_storage(symbol_table_);
  // Stats::StatName prefix = stat_name_storage.add(stripTrailingDot(info.prefix_));
  Code code = static_cast<Code>(info.response_status_code_);

  ASSERT(&info.cluster_scope_.symbolTable() == &symbol_table_);
  chargeBasicResponseStat(info.cluster_scope_, info.prefix_, code);

  Stats::StatName rq_group = upstreamRqGroup(code);
  Stats::StatName rq_code = upstream_rq_.statName(code);

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
    Stats::StatName vhost_name = info.request_vhost_name_;
    Stats::StatName vcluster_name = info.request_vcluster_name_;

    incCounter(info.global_scope_,
               {vhost_, vhost_name, vcluster_, vcluster_name, upstream_rq_completed_});
    incCounter(info.global_scope_, {vhost_, vhost_name, vcluster_, vcluster_name, rq_group});
    incCounter(info.global_scope_, {vhost_, vhost_name, vcluster_, vcluster_name, rq_code});
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    Stats::StatName from_zone = info.from_zone_;
    Stats::StatName to_zone = info.to_zone_;

    incCounter(info.cluster_scope_,
               {info.prefix_, zone_, from_zone, to_zone, upstream_rq_completed_});
    incCounter(info.cluster_scope_, {info.prefix_, zone_, from_zone, to_zone, rq_group});
    incCounter(info.cluster_scope_, {info.prefix_, zone_, from_zone, to_zone, rq_code});
  }
}

void CodeStatsImpl::chargeResponseTiming(const ResponseTimingInfo& info) const {
  Stats::StatNameManagedContainer stat_name_storage(symbol_table_);
  // Stats::StatName prefix = stat_name_storage.add(stripTrailingDot(info.prefix_));

  uint64_t count = info.response_time_.count();
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
    Stats::StatName vhost_name = info.request_vhost_name_;
    Stats::StatName vcluster_name = info.request_vcluster_name_;
    recordHistogram(info.global_scope_,
                    {vhost_, vhost_name, vcluster_, vcluster_name, upstream_rq_time_}, count);
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    Stats::StatName from_zone = info.from_zone_;
    Stats::StatName to_zone = info.to_zone_;

    recordHistogram(info.cluster_scope_,
                    {info.prefix_, zone_, from_zone, to_zone, upstream_rq_time_}, count);
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

CodeStatsImpl::RequestCodeGroup::RequestCodeGroup(absl::string_view prefix,
                                                  CodeStatsImpl& code_stats)
    : code_stats_(code_stats), prefix_(std::string(prefix)),
      stat_name_storage_(code_stats_.symbol_table_) {
  for (uint32_t i = 0; i < NumHttpCodes; ++i) {
    rc_stat_names_[i] = nullptr;
  }

  // Pre-allocate response codes 200, 404, and 503, as those seem quite likely.
  statName(Code::OK);
  statName(Code::NotFound);
  statName(Code::ServiceUnavailable);
}

Stats::StatName CodeStatsImpl::RequestCodeGroup::statName(Code response_code) {
  // Take a lock only if we've never seen this response-code before.
  uint32_t rc_int = static_cast<uint32_t>(response_code);
  RELEASE_ASSERT(rc_int < NumHttpCodes, absl::StrCat("Unexpected http code: ", rc_int));
  std::atomic<uint8_t*>& atomic_ref = rc_stat_names_[rc_int];
  if (atomic_ref.load() == nullptr) {
    absl::MutexLock lock(&mutex_);

    // Check again under lock as two threads might have raced to add a StatName
    // for the same code.
    if (atomic_ref.load() == nullptr) {
      atomic_ref =
          stat_name_storage_.addReturningStorage(absl::StrCat(prefix_, enumToInt(response_code)));
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
