#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/policies.pb.h"

#include "source/extensions/filters/http/custom_response/policy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class RedirectFactory : public PolicyMatchActionFactory<
                            envoy::extensions::filters::http::custom_response::v3::RedirectPolicy> {
public:
  ~RedirectFactory() override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  PolicySharedPtr createPolicy(const Protobuf::Message& config,
                               Envoy::Server::Configuration::ServerFactoryContext& server,
                               Stats::StatName stats_prefix) override;

  std::string name() const override;
};

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
