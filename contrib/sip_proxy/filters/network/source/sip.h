#pragma once
#include <map>

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

enum class MethodType {
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
  // FBThrift values.
  // See https://github.com/facebook/fbthrift/blob/master/thrift/lib/cpp/TApplicationException.h#L52
  UnsupportedClientType = 10,
  LoadShedding = 11,
  Timeout = 12,
  InjectedFailure = 13,
  ChecksumMismatch = 14,
  Interruption = 15,
};

class HeaderTypeMap {
public:
  HeaderTypeMap() {
    sip_header_type_map_.insert({"Call-ID", HeaderType::CallId});
    sip_header_type_map_.insert({"Via", HeaderType::Via});
    sip_header_type_map_.insert({"To", HeaderType::To});
    sip_header_type_map_.insert({"From", HeaderType::From});
    sip_header_type_map_.insert({"Contact", HeaderType::Contact});
    sip_header_type_map_.insert({"Record-Route", HeaderType::RRoute});
    sip_header_type_map_.insert({"CSeq", HeaderType::Cseq});
    sip_header_type_map_.insert({"Route", HeaderType::Route});
    sip_header_type_map_.insert({"Path", HeaderType::Path});
    sip_header_type_map_.insert({"Event", HeaderType::Event});
    sip_header_type_map_.insert({"Service-Route", HeaderType::SRoute});
    sip_header_type_map_.insert({"WWW-Authenticate", HeaderType::WAuth});
    sip_header_type_map_.insert({"Authorization", HeaderType::Auth});
    sip_header_type_map_.insert({"P-Nokia-Cookie-IP-Mapping", HeaderType::PCookieIPMap});
    sip_header_type_map_.insert({"", HeaderType::Other});
  }
  std::map<absl::string_view, HeaderType> headerTypeMap() { return sip_header_type_map_; }

private:
  std::map<absl::string_view, HeaderType> sip_header_type_map_{};
};

static HeaderTypeMap type_map;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
