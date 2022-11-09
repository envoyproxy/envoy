#pragma once

#include "envoy/extensions/http/early_header_mutation/regex_mutation/v3/regex_mutation.pb.h"

#include "source/extensions/http/early_header_mutation/regex_mutation/regex_mutation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace RegexMutation {

class Factory : public Envoy::Http::EarlyHeaderMutationFactory {
public:
  std::string name() const override { return "envoy.http.early_header_mutation.regex_mutation"; }

  Envoy::Http::EarlyHeaderMutationPtr
  createExtension(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::early_header_mutation::regex_mutation::v3::RegexMutation>();
  }
};

} // namespace RegexMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
