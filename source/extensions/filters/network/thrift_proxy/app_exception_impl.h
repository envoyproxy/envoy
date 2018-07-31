#pragma once

#include "extensions/filters/network/thrift_proxy/filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

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
  UnsupportedClientType = 10,
};

struct AppException : public ThriftFilters::DirectResponse {
  AppException(const absl::string_view method_name, int32_t seq_id, AppExceptionType type,
               const std::string& error_message)
      : method_name_(method_name), seq_id_(seq_id), type_(type), error_message_(error_message) {}

  void encode(ThriftProxy::Protocol& proto, Buffer::Instance& buffer) override;

  const std::string method_name_;
  const int32_t seq_id_;
  const AppExceptionType type_;
  const std::string error_message_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
