#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// Interface to inject custom logic for building attributes for the external
// processor.
class AttributeBuilder {
public:
  virtual ~AttributeBuilder() = default;

  struct BuildParams {
    envoy::config::core::v3::TrafficDirection traffic_direction;
    StreamInfo::StreamInfo& stream_info;
    const Http::RequestHeaderMap* request_headers;
    const Http::RequestOrResponseHeaderMap* response_headers;
    const Http::HeaderMap* response_trailers;
  };

  // Called to build attributes.
  virtual absl::optional<Protobuf::Struct> build(const BuildParams& params) const PURE;
};

class AttributeBuilderFactory : public Config::TypedFactory {
public:
  ~AttributeBuilderFactory() override = default;
  virtual std::unique_ptr<AttributeBuilder> createAttributeBuilder(
      const Protobuf::Message& config,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder,
      Server::Configuration::CommonFactoryContext& context) const PURE;

  std::string category() const override { return "envoy.http.ext_proc.attribute_builders"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
