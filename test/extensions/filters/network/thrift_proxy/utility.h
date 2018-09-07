#pragma once

#include <initializer_list>

#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"

#include "extensions/filters/network/thrift_proxy/thrift.h"

#include "test/common/buffer/utility.h"

#include "absl/strings/ascii.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::TestParamInfo;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

using Envoy::Buffer::addRepeated;
using Envoy::Buffer::addSeq;

inline std::string fieldTypeToString(const FieldType& field_type) {
  switch (field_type) {
  case FieldType::Stop:
    return "Stop";
  case FieldType::Void:
    return "Void";
  case FieldType::Bool:
    return "Bool";
  case FieldType::Byte:
    return "Byte";
  case FieldType::Double:
    return "Double";
  case FieldType::I16:
    return "I16";
  case FieldType::I32:
    return "I32";
  case FieldType::I64:
    return "I64";
  case FieldType::String:
    return "String";
  case FieldType::Struct:
    return "Struct";
  case FieldType::Map:
    return "Map";
  case FieldType::Set:
    return "Set";
  case FieldType::List:
    return "List";
  default:
    return "UnknownFieldType";
  }
}

inline std::string fieldTypeParamToString(const TestParamInfo<FieldType>& params) {
  return fieldTypeToString(params.param);
}

inline envoy::config::filter::network::thrift_proxy::v2alpha1::TransportType
transportTypeToProto(TransportType transport_type) {
  switch (transport_type) {
  case TransportType::Framed:
    return envoy::config::filter::network::thrift_proxy::v2alpha1::TransportType::FRAMED;
  case TransportType::Unframed:
    return envoy::config::filter::network::thrift_proxy::v2alpha1::TransportType::UNFRAMED;
  case TransportType::Header:
    return envoy::config::filter::network::thrift_proxy::v2alpha1::TransportType::HEADER;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

inline envoy::config::filter::network::thrift_proxy::v2alpha1::ProtocolType
protocolTypeToProto(ProtocolType protocol_type) {
  switch (protocol_type) {
  case ProtocolType::Binary:
    return envoy::config::filter::network::thrift_proxy::v2alpha1::ProtocolType::BINARY;
  case ProtocolType::Compact:
    return envoy::config::filter::network::thrift_proxy::v2alpha1::ProtocolType::COMPACT;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

inline std::string transportNameForTest(TransportType transport_type) {
  std::string name = TransportNames::get().fromType(transport_type);
  name[0] = absl::ascii_toupper(name[0]);
  return name;
}

inline std::string protocolNameForTest(ProtocolType protocol_type) {
  std::string name = ProtocolNames::get().fromType(protocol_type);
  name[0] = absl::ascii_toupper(name[0]);
  return name;
}

MATCHER(IsEmptyMetadata, "") {
  if (arg.hasFrameSize()) {
    *result_listener << "has a frame size of " << arg.frameSize();
    return false;
  }
  if (arg.hasProtocol()) {
    *result_listener << "has a protocol of " << ProtocolNames::get().fromType(arg.protocol());
    return false;
  }
  if (arg.hasMethodName()) {
    *result_listener << "has a method name of " << arg.methodName();
    return false;
  }
  if (arg.hasSequenceId()) {
    *result_listener << "has a sequence id " << arg.sequenceId();
    return false;
  }
  if (arg.hasMessageType()) {
    *result_listener << "has a message type of " << static_cast<int>(arg.messageType());
    return false;
  }
  if (arg.headers().size() > 0) {
    *result_listener << "has " << arg.headers().size() << " headers";
    return false;
  }
  if (arg.hasAppException()) {
    *result_listener << "has an app exception";
    return false;
  }
  return true;
}

MATCHER_P(HasOnlyFrameSize, n, "") {
  return arg.hasFrameSize() && arg.frameSize() == n && !arg.hasProtocol() && !arg.hasMethodName() &&
         !arg.hasSequenceId() && !arg.hasMessageType() && arg.headers().size() == 0 &&
         !arg.hasAppException();
}

MATCHER_P(HasFrameSize, n, "") {
  if (!arg.hasFrameSize()) {
    *result_listener << "has no frame size";
    return false;
  }
  *result_listener << "has frame size = " << arg.frameSize();
  return arg.frameSize() == n;
}

MATCHER_P(HasProtocol, p, "") { return arg.hasProtocol() && arg.protocol() == p; }
MATCHER_P(HasSequenceId, id, "") { return arg.hasSequenceId() && arg.sequenceId() == id; }
MATCHER(HasNoHeaders, "") { return arg.headers().size() == 0; }

MATCHER_P2(HasAppException, t, m, "") {
  if (!arg.hasAppException()) {
    *result_listener << "has no exception";
    return false;
  }

  if (arg.appExceptionType() != t) {
    *result_listener << "has exception with type " << static_cast<int>(arg.appExceptionType());
    return false;
  }

  if (std::string(m) != arg.appExceptionMessage()) {
    *result_listener << "has exception with message " << arg.appExceptionMessage();
    return false;
  }

  return true;
}

} // namespace
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
