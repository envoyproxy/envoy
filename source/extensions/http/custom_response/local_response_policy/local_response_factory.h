#pragma once

#include "envoy/extensions/http/custom_response/local_response_policy/v3/local_response_policy.pb.h"

#include "source/extensions/filters/http/custom_response/policy.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace CustomResponse {

class LocalResponseFactory
    : public Extensions::HttpFilters::CustomResponse::PolicyMatchActionFactory<
          envoy::extensions::http::custom_response::local_response_policy::v3::
              LocalResponsePolicy> {
public:
  Extensions::HttpFilters::CustomResponse::PolicySharedPtr
  createPolicy(const Protobuf::Message& config,
               Envoy::Server::Configuration::ServerFactoryContext& server,
               Stats::StatName stats_prefix) override;

  std::string name() const override;
};

} // namespace CustomResponse
} // namespace Http
} // namespace Extensions
} // namespace Envoy
