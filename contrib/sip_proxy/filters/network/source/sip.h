#pragma once

#include "source/common/singleton/const_singleton.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

enum HeaderType {
  TopLine,
  CallId,
  Via,
  To,
  From,
  Route,
  Contact,
  RRoute,
  Cseq,
  Path,
  Event,
  SRoute,
  WAuth,
  Auth,
  PCookieIPMap,
  Other,
  InvalidFormat,
  HeaderMaxNum
};

enum class MsgType { Request, Response, ErrorMsg };

enum MethodType {
  Invite,
  Register,
  Update,
  Refer,
  Subscribe,
  Notify,
  Ack,
  Bye,
  Cancel,
  Ok200,
  Failure4xx,
  OtherMethod,
  NullMethod
};

enum class AppExceptionType {
  Unknown = 0,
  UnknownMethod = 1,
  InvalidMessageType = 2,
  WrongMethodName = 3,
  BadSequenceId = 4,
  MissingResult = 5,
  InternalError = 6,
  ProtocolError = 7,
  InvalidTransform = 8,
  InvalidProtocol = 9,
  UnsupportedClientType = 10,
  LoadShedding = 11,
  Timeout = 12,
  InjectedFailure = 13,
  ChecksumMismatch = 14,
  Interruption = 15,
};

class HeaderTypeMap {
public:
  HeaderType str2Header(const absl::string_view& header) const {
    if (const auto& result = sip_header_type_map_.find(header);
        result != sip_header_type_map_.end()) {
      return result->second;
    } else {
      return HeaderType::Other;
    }
  }

  HeaderType str2Header(const std::string& header) const {
    if (const auto& result = sip_header_type_map_.find(header);
        result != sip_header_type_map_.end()) {
      return result->second;
    } else {
      return HeaderType::Other;
    }
  }

private:
  const absl::flat_hash_map<absl::string_view, HeaderType> sip_header_type_map_{
      {"Call-ID", HeaderType::CallId},
      {"Via", HeaderType::Via},
      {"To", HeaderType::To},
      {"From", HeaderType::From},
      {"Contact", HeaderType::Contact},
      {"Record-Route", HeaderType::RRoute},
      {"CSeq", HeaderType::Cseq},
      {"Route", HeaderType::Route},
      {"Path", HeaderType::Path},
      {"Event", HeaderType::Event},
      {"Service-Route", HeaderType::SRoute},
      {"WWW-Authenticate", HeaderType::WAuth},
      {"Authorization", HeaderType::Auth},
      {"TopLine", HeaderType::TopLine},
      {"P-Nokia-Cookie-IP-Mapping", HeaderType::PCookieIPMap}};
};

using HeaderTypes = ConstSingleton<HeaderTypeMap>;
extern std::vector<std::string> methodStr;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
