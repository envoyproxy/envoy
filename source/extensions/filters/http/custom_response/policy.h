#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class CustomResponseFilter;

class Policy : public std::enable_shared_from_this<Policy>, public StreamInfo::FilterState::Object {

public:
  virtual ~Policy() = default;

  virtual Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool,
                                                  CustomResponseFilter&) const PURE;

protected:
  Policy() = default;
};

using PolicySharedPtr = std::shared_ptr<Policy>;

class PolicyFactory : public Config::TypedFactory {
public:
  ~PolicyFactory() override = default;

  virtual PolicySharedPtr createPolicy(const Protobuf::Message& config,
                                       Envoy::Server::Configuration::ServerFactoryContext& context,
                                       Stats::StatName stats_prefix) PURE;

  std::string category() const override {
    return "envoy.extensions.http.filters.custom_response.policy";
  }
};
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
