#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/ext_proc/on_receive_message_decorator.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExtProcTestUtility {
public:
  // Compare a reference header map to a proto
  static bool headerProtosEqualIgnoreOrder(const ::Envoy::Http::HeaderMap& expected,
                                           const envoy::config::core::v3::HeaderMap& actual);

private:
  // These headers are present in the actual, but cannot be specified in the expected
  // ignoredHeaders should not be used for equal comparison
  static const absl::flat_hash_set<std::string> ignoredHeaders();
};

MATCHER_P(HeaderProtosEqual, expected, "HTTP header protos match") {
  return ExtProcTestUtility::headerProtosEqualIgnoreOrder(expected, arg);
}

MATCHER_P(HasNoHeader, key, absl::StrFormat("Headers have no value for \"%s\"", key)) {
  return arg.get(::Envoy::Http::LowerCaseString(std::string(key))).empty();
}

MATCHER_P(HasHeader, key, absl::StrFormat("There exists a header for \"%s\"", key)) {
  return !arg.get(::Envoy::Http::LowerCaseString(std::string(key))).empty();
}

MATCHER_P2(SingleHeaderValueIs, key, value,
           absl::StrFormat("Header \"%s\" equals \"%s\"", key, value)) {
  const auto hdr = arg.get(::Envoy::Http::LowerCaseString(std::string(key)));
  if (hdr.size() != 1) {
    return false;
  }
  return hdr[0]->value() == value;
}

MATCHER_P2(SingleProtoHeaderValueIs, key, value,
           absl::StrFormat("Header \"%s\" equals \"%s\"", key, value)) {
  for (const auto& hdr : arg.headers()) {
    if (key == hdr.key()) {
      return value == hdr.value();
    }
  }
  return false;
}

envoy::config::core::v3::HeaderValue makeHeaderValue(const std::string& key,
                                                     const std::string& value);

class TestOnReceiveMessageDecorator : public OnReceiveMessageDecorator {
public:
  void onReceiveMessage(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                        absl::Status processing_status, Envoy::StreamInfo::StreamInfo&) override;
};

class TestOnReceiveMessageDecoratorFactory : public OnReceiveMessageDecoratorFactory {
public:
  ~TestOnReceiveMessageDecoratorFactory() override = default;
  std::unique_ptr<OnReceiveMessageDecorator>
  createDecorator(const Protobuf::Message&,
                  const Envoy::Server::Configuration::CommonFactoryContext&) const override {
    return std::make_unique<TestOnReceiveMessageDecorator>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom filter config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "on_receive_message_decorator"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
