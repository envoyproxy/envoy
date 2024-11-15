#pragma once

#include "envoy/extensions/http/early_header_mutation/header_mutation/v3/header_mutation.pb.h"
#include "envoy/extensions/http/early_header_mutation/header_mutation/v3/header_mutation.pb.validate.h"

#include "source/extensions/http/early_header_mutation/header_mutation/header_mutation.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {

class Factory : public Envoy::Http::EarlyHeaderMutationFactory {
public:
  std::string name() const override { return "envoy.http.early_header_mutation.header_mutation"; }

  Envoy::Http::EarlyHeaderMutationPtr
  createExtension(const Protobuf::Message& config,
                  Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::early_header_mutation::header_mutation::v3::HeaderMutation>();
  }
};

} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
