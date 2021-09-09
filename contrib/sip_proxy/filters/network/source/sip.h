#pragma once

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

enum class HeaderType {
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
  Other,
  InvalidFormat
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

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
