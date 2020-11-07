#pragma once

#include "common/common/assert.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

enum class TransportType {
  Framed,
  Header,
  Unframed,
  Auto,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST TRANSPORT TYPE
  LastTransportType = Auto,
};

/**
 * Names of available Transport implementations.
 */
class TransportNameValues {
public:
  // Framed transport
  const std::string FRAMED = "framed";

  // Header transport
  const std::string HEADER = "header";

  // Unframed transport
  const std::string UNFRAMED = "unframed";

  // Auto-detection transport
  const std::string AUTO = "auto";

  const std::string& fromType(TransportType type) const {
    switch (type) {
    case TransportType::Framed:
      return FRAMED;
    case TransportType::Header:
      return HEADER;
    case TransportType::Unframed:
      return UNFRAMED;
    case TransportType::Auto:
      return AUTO;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
};

using TransportNames = ConstSingleton<TransportNameValues>;

enum class ProtocolType {
  Binary,
  LaxBinary,
  Compact,
  Twitter,
  Auto,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST PROTOCOL TYPE
  LastProtocolType = Auto,
};

/**
 * Names of available Protocol implementations.
 */
class ProtocolNameValues {
public:
  // Binary protocol
  const std::string BINARY = "binary";

  // Lax Binary protocol
  const std::string LAX_BINARY = "binary/non-strict";

  // Compact protocol
  const std::string COMPACT = "compact";

  // Twitter protocol
  const std::string TWITTER = "twitter";

  // Auto-detection protocol
  const std::string AUTO = "auto";

  const std::string& fromType(ProtocolType type) const {
    switch (type) {
    case ProtocolType::Binary:
      return BINARY;
    case ProtocolType::LaxBinary:
      return LAX_BINARY;
    case ProtocolType::Compact:
      return COMPACT;
    case ProtocolType::Twitter:
      return TWITTER;
    case ProtocolType::Auto:
      return AUTO;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
};

using ProtocolNames = ConstSingleton<ProtocolNameValues>;

/**
 * Thrift protocol message types.
 * See https://github.com/apache/thrift/blob/master/lib/cpp/src/thrift/protocol/TProtocol.h
 */
enum class MessageType {
  Call = 1,
  Reply = 2,
  Exception = 3,
  Oneway = 4,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST MESSAGE TYPE
  LastMessageType = Oneway,
};

/**
 * Thrift protocol struct field types.
 * See https://github.com/apache/thrift/blob/master/lib/cpp/src/thrift/protocol/TProtocol.h
 */
enum class FieldType {
  Stop = 0,
  Void = 1,
  Bool = 2,
  Byte = 3,
  Double = 4,
  I16 = 6,
  I32 = 8,
  I64 = 10,
  String = 11,
  Struct = 12,
  Map = 13,
  Set = 14,
  List = 15,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST FIELD TYPE
  LastFieldType = List,
};

/**
 * Thrift Application Exception types.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-rpc.md
 */
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

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
