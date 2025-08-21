#pragma once

#include "envoy/extensions/matching/common_inputs/ssl/v3/ssl_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/ssl/v3/ssl_inputs.pb.validate.h"
#include "envoy/matcher/matcher.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

template <class InputType, class ProtoType, class MatchingDataType>
class BaseFactory : public Matcher::DataInputFactory<MatchingDataType> {
protected:
  explicit BaseFactory(const std::string& name) : name_(name) {}

public:
  std::string name() const override { return "envoy.matching.inputs." + name_; }

  Matcher::DataInputFactoryCb<MatchingDataType>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<InputType>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoType>();
  }

private:
  const std::string name_;
};

template <class MatchingDataType> class UriSanInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& ssl = data.ssl();
    if (!ssl) {
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
    }
    const auto& uri = ssl->uriSanPeerCertificate();
    if (!uri.empty()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              absl::StrJoin(uri, ",")};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
  }
};

template <class MatchingDataType>
class UriSanInputBaseFactory
    : public BaseFactory<UriSanInput<MatchingDataType>,
                         envoy::extensions::matching::common_inputs::ssl::v3::UriSanInput,
                         MatchingDataType> {
public:
  UriSanInputBaseFactory()
      : BaseFactory<UriSanInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::ssl::v3::UriSanInput,
                    MatchingDataType>("uri_san") {}
};

template <class MatchingDataType> class DnsSanInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& ssl = data.ssl();
    if (!ssl) {
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
    }
    const auto& dns = ssl->dnsSansPeerCertificate();
    if (!dns.empty()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              absl::StrJoin(dns, ",")};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
  }
};

template <class MatchingDataType>
class DnsSanInputBaseFactory
    : public BaseFactory<DnsSanInput<MatchingDataType>,
                         envoy::extensions::matching::common_inputs::ssl::v3::DnsSanInput,
                         MatchingDataType> {
public:
  DnsSanInputBaseFactory()
      : BaseFactory<DnsSanInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::ssl::v3::DnsSanInput,
                    MatchingDataType>("dns_san") {}
};

template <class MatchingDataType> class SubjectInput : public Matcher::DataInput<MatchingDataType> {
public:
  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto& ssl = data.ssl();
    if (!ssl) {
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
    }
    const auto& subject = ssl->subjectPeerCertificate();
    if (!subject.empty()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              std::string(subject)};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
  }
};

template <class MatchingDataType>
class SubjectInputBaseFactory
    : public BaseFactory<SubjectInput<MatchingDataType>,
                         envoy::extensions::matching::common_inputs::ssl::v3::SubjectInput,
                         MatchingDataType> {
public:
  SubjectInputBaseFactory()
      : BaseFactory<SubjectInput<MatchingDataType>,
                    envoy::extensions::matching::common_inputs::ssl::v3::SubjectInput,
                    MatchingDataType>("subject") {}
};

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
