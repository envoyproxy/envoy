#pragma once

#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/common/assert.h"
#include "source/common/singleton/const_singleton.h"

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
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
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
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
};

using ProtocolNames = ConstSingleton<ProtocolNameValues>;

class ProtoUtils {
public:
  static TransportType
  getTransportType(envoy::extensions::filters::network::thrift_proxy::v3::TransportType transport) {
    switch (transport) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::extensions::filters::network::thrift_proxy::v3::AUTO_TRANSPORT:
      return TransportType::Auto;
    case envoy::extensions::filters::network::thrift_proxy::v3::FRAMED:
      return TransportType::Framed;
    case envoy::extensions::filters::network::thrift_proxy::v3::UNFRAMED:
      return TransportType::Unframed;
    case envoy::extensions::filters::network::thrift_proxy::v3::HEADER:
      return TransportType::Header;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  static ProtocolType
  getProtocolType(envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType protocol) {
    switch (protocol) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::extensions::filters::network::thrift_proxy::v3::AUTO_PROTOCOL:
      return ProtocolType::Auto;
    case envoy::extensions::filters::network::thrift_proxy::v3::BINARY:
      return ProtocolType::Binary;
    case envoy::extensions::filters::network::thrift_proxy::v3::LAX_BINARY:
      return ProtocolType::LaxBinary;
    case envoy::extensions::filters::network::thrift_proxy::v3::COMPACT:
      return ProtocolType::Compact;
    case envoy::extensions::filters::network::thrift_proxy::v3::TWITTER:
      return ProtocolType::Twitter;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
};

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
 * Names of available message types.
 */
class MessageTypeNameValues {
public:
  // Call (regular request that awaits for a response)
  const std::string CALL = "call";

  // Reply (or response)
  const std::string REPLY = "reply";

  // Exception (generated instead of reply)
  const std::string EXCEPTION = "exception";

  // Oneway (no reply expected)
  const std::string ONEWAY = "oneway";

  const std::string& fromType(MessageType type) const {
    switch (type) {
    case MessageType::Call:
      return CALL;
    case MessageType::Reply:
      return REPLY;
    case MessageType::Exception:
      return EXCEPTION;
    case MessageType::Oneway:
      return ONEWAY;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
};

using MessageTypeNames = ConstSingleton<MessageTypeNameValues>;

/**
 * A Reply message is either a success or an error (IDL exception)
 */
enum class ReplyType {
  Success,
  Error,
};

/**
 * Names of available reply types.
 */
class ReplyTypeNameValues {
public:
  // Success
  const std::string SUCCESS = "success";

  // Error
  const std::string ERROR = "error";

  const std::string& fromType(ReplyType type) const {
    switch (type) {
    case ReplyType::Success:
      return SUCCESS;
    case ReplyType::Error:
      return ERROR;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
};

using ReplyTypeNames = ConstSingleton<ReplyTypeNameValues>;

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

/**
 * Names of pool failure reason.
 */
class PoolFailureReasonNameValues {
public:
  const std::string OVERFLOW_NAME = "overflow";
  const std::string LOCAL_CONNECTION_FAILURE_NAME = "local connection failure";
  const std::string REMOTE_CONNECTION_FAILURE_NAME = "remote connection failure";
  const std::string TIMEOUT_NAME = "timeout";

  const std::string& fromReason(ConnectionPool::PoolFailureReason reason) const {
    switch (reason) {
    case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
      return LOCAL_CONNECTION_FAILURE_NAME;
    case ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
      return REMOTE_CONNECTION_FAILURE_NAME;
    case ConnectionPool::PoolFailureReason::Timeout:
      return TIMEOUT_NAME;
    case ConnectionPool::PoolFailureReason::Overflow:
      return OVERFLOW_NAME;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
};
using PoolFailureReasonNames = ConstSingleton<PoolFailureReasonNameValues>;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
