#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/ext_proc/matching_utils.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

// Interface to modify the processing request before sending it to the ext_proc backend
class ProcessingRequestModifier {
public:
  virtual ~ProcessingRequestModifier() = default;

  struct Params {

    envoy::config::core::v3::TrafficDirection traffic_direction;
    Http::StreamFilterCallbacks* callbacks;
    const Http::RequestHeaderMap* request_headers;
    const Http::RequestOrResponseHeaderMap* response_headers;
    const Http::HeaderMap* response_trailers;
  };

  // Called to modify the request after it is created, but before it is sent on the wire.
  // Implementations may modify the request and must return true if any modifications were made.
  virtual bool
  modifyRequest(const Params& params,
                envoy::service::ext_proc::v3::ProcessingRequest& processingRequest) PURE;
};

class ProcessingRequestModifierFactory : public Config::TypedFactory {
public:
  /**
   * Creates a new attribute builder based on the type supplied by the config.
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
  virtual std::unique_ptr<ProcessingRequestModifier> createProcessingRequestModifier(
      const Protobuf::Message& config,
      Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr expr_builder,
      Server::Configuration::CommonFactoryContext& context) const PURE;

  std::string category() const override {
    return "envoy.http.ext_proc.processing_request_modifiers";
  }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
