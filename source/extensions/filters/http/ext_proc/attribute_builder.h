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

  // Called to build attributes. Takes in the params needed to create the attributes.
  // Implementations are expected to set attributes and then return true iff any were set.
  virtual bool build(const BuildParams& params,
                     Protobuf::Map<std::string, Protobuf::Struct>* attributes) const PURE;
};

class AttributeBuilderFactory : public Config::TypedFactory {
public:
  ~AttributeBuilderFactory() override = default;

  /**
   * Creates a new attribute builder based on the type supplied by the confg.
   *
   * @param config The config passed from the ExternalProcessing proto. Contains the builder type.
   * @param default_attribute_key The default attribute key to use when setting request and response
   * attributes. Custom attributes builders can choose to ignore this and use their own key space.
   * @param expr_builder The builder to use for CEL expression construction. This allows custom
   * attribute builders to more easily build attributes from CEL expressions. Other implementations
   * are possible, in which case this can be ignored.
   * @param context The main server context.
   * @return Status of the operation
   */
  virtual std::unique_ptr<AttributeBuilder> createAttributeBuilder(
      const Protobuf::Message& config, absl::string_view default_attribute_key,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder,
      Server::Configuration::CommonFactoryContext& context) const PURE;

  std::string category() const override { return "envoy.http.ext_proc.attribute_builders"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
