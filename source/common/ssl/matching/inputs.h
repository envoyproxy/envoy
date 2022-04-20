#pragma once

#include "envoy/extensions/matching/common_inputs/ssl/v3/ssl_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/ssl/v3/ssl_inputs.pb.validate.h"
#include "envoy/matcher/matcher.h"

#include "source/common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

template <class MatchingDataType>
class AuthenticatedInput : public Matcher::DataInput<MatchingDataType> {
public:
  explicit AuthenticatedInput(
      envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput::Field field)
      : field_(field) {}

  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& ssl = data.ssl();
    if (!ssl) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
    }

    switch (field_) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput::UriSan: {
      const auto& uri = ssl->uriSanPeerCertificate();
      if (!uri.empty()) {
        return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
                absl::StrJoin(uri, ",")};
      }
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
    }
    case envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput::DnsSan: {
      const auto& dns = ssl->dnsSansPeerCertificate();
      if (!dns.empty()) {
        return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
                absl::StrJoin(dns, ",")};
      }
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
    }
    case envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput::Subject: {
      const auto& subject = ssl->subjectPeerCertificate();
      if (!subject.empty()) {
        return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
                ssl->subjectPeerCertificate()};
      }
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
    }
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

private:
  const envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput::Field field_;
};

template <class MatchingDataType>
class AuthenticatedInputFactory : public Matcher::DataInputFactory<MatchingDataType> {
public:
  std::string name() const override { return "envoy.matching.inputs.authenticated"; }

  Matcher::DataInputFactoryCb<MatchingDataType>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput&>(
        config, validation_visitor);

    return [field = typed_config.field()] {
      return std::make_unique<AuthenticatedInput<MatchingDataType>>(field);
    };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::ssl::v3::AuthenticatedInput>();
  }
};

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
