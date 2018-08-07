#include "common/http/codes.h"

#include <cstdint>
#include <string>

#include "envoy/http/header_map.h"
#include "envoy/stats/scope.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Http {

void CodeUtility::chargeBasicResponseStat(Stats::Scope& scope, const std::string& prefix,
                                          Code response_code) {
  // Build a dynamic stat for the response code and increment it.
  scope.counter(fmt::format("{}upstream_rq_{}", prefix, groupStringForResponseCode(response_code)))
      .inc();
  scope.counter(fmt::format("{}upstream_rq_{}", prefix, enumToInt(response_code))).inc();
}

void CodeUtility::chargeResponseStat(const ResponseStatInfo& info) {
  const uint64_t response_code = info.response_status_code_;
  chargeBasicResponseStat(info.cluster_scope_, info.prefix_, static_cast<Code>(response_code));

  std::string group_string = groupStringForResponseCode(static_cast<Code>(response_code));

  // If the response is from a canary, also create canary stats.
  if (info.upstream_canary_) {
    info.cluster_scope_.counter(fmt::format("{}canary.upstream_rq_{}", info.prefix_, group_string))
        .inc();
    info.cluster_scope_.counter(fmt::format("{}canary.upstream_rq_{}", info.prefix_, response_code))
        .inc();
  }

  // Split stats into external vs. internal.
  if (info.internal_request_) {
    info.cluster_scope_
        .counter(fmt::format("{}internal.upstream_rq_{}", info.prefix_, group_string))
        .inc();
    info.cluster_scope_
        .counter(fmt::format("{}internal.upstream_rq_{}", info.prefix_, response_code))
        .inc();
  } else {
    info.cluster_scope_
        .counter(fmt::format("{}external.upstream_rq_{}", info.prefix_, group_string))
        .inc();
    info.cluster_scope_
        .counter(fmt::format("{}external.upstream_rq_{}", info.prefix_, response_code))
        .inc();
  }

  // Handle request virtual cluster.
  if (!info.request_vcluster_name_.empty()) {
    info.global_scope_
        .counter(fmt::format("vhost.{}.vcluster.{}.upstream_rq_{}", info.request_vhost_name_,
                             info.request_vcluster_name_, group_string))
        .inc();
    info.global_scope_
        .counter(fmt::format("vhost.{}.vcluster.{}.upstream_rq_{}", info.request_vhost_name_,
                             info.request_vcluster_name_, response_code))
        .inc();
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    info.cluster_scope_
        .counter(fmt::format("{}zone.{}.{}.upstream_rq_{}", info.prefix_, info.from_zone_,
                             info.to_zone_, group_string))
        .inc();
    info.cluster_scope_
        .counter(fmt::format("{}zone.{}.{}.upstream_rq_{}", info.prefix_, info.from_zone_,
                             info.to_zone_, response_code))
        .inc();
  }
}

void CodeUtility::chargeResponseTiming(const ResponseTimingInfo& info) {
  info.cluster_scope_.histogram(info.prefix_ + "upstream_rq_time")
      .recordValue(info.response_time_.count());
  if (info.upstream_canary_) {
    info.cluster_scope_.histogram(info.prefix_ + "canary.upstream_rq_time")
        .recordValue(info.response_time_.count());
  }

  if (info.internal_request_) {
    info.cluster_scope_.histogram(info.prefix_ + "internal.upstream_rq_time")
        .recordValue(info.response_time_.count());
  } else {
    info.cluster_scope_.histogram(info.prefix_ + "external.upstream_rq_time")
        .recordValue(info.response_time_.count());
  }

  if (!info.request_vcluster_name_.empty()) {
    info.global_scope_
        .histogram("vhost." + info.request_vhost_name_ + ".vcluster." +
                   info.request_vcluster_name_ + ".upstream_rq_time")
        .recordValue(info.response_time_.count());
  }

  // Handle per zone stats.
  if (!info.from_zone_.empty() && !info.to_zone_.empty()) {
    info.cluster_scope_
        .histogram(fmt::format("{}zone.{}.{}.upstream_rq_time", info.prefix_, info.from_zone_,
                               info.to_zone_))
        .recordValue(info.response_time_.count());
  }
}

std::string CodeUtility::groupStringForResponseCode(Code response_code) {
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
