#pragma once

#include <memory>
#include <vector>

#include "envoy/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/v3/mapped_attribute_builder.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/ext_proc/processing_request_modifier.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace ExternalProcessing {

// Provides an alternative way of constructing request attributes. Internally, it uses CEL
// evaluation. The difference is it allows for a custom keys, unlike the base implementation.
// Assumes that all Params are populated. Attributes are only passed once per the lifetime of
// this object, in line with the original impl.
class MappedAttributeBuilder
    : public Envoy::Extensions::HttpFilters::ExternalProcessing::ProcessingRequestModifier {
public:
  MappedAttributeBuilder(const envoy::extensions::http::ext_proc::processing_request_modifiers::
                             mapped_attribute_builder::v3::MappedAttributeBuilder& config,
                         Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
                         Server::Configuration::CommonFactoryContext& context);

  bool modifyRequest(const Params& params,
                     envoy::service::ext_proc::v3::ProcessingRequest& request) override;

private:
  const envoy::extensions::http::ext_proc::processing_request_modifiers::mapped_attribute_builder::
      v3::MappedAttributeBuilder config_;
  const Extensions::HttpFilters::ExternalProcessing::ExpressionManager expression_manager_;
  bool sent_request_attributes_ = false;
};

} // namespace ExternalProcessing
} // namespace Http
} // namespace Envoy
