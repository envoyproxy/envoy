#include "source/common/tracing/tracer_config_impl.h"

namespace Envoy {
namespace Tracing {

ConnectionManagerTracingConfig::ConnectionManagerTracingConfig(
    envoy::config::core::v3::TrafficDirection traffic_direction,
    const ConnectionManagerTracingConfigProto& tracing_config,
    const Formatter::CommandParserPtrVector& command_parsers) {

  // Listener level traffic direction overrides the operation name
  switch (traffic_direction) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::core::v3::UNSPECIFIED:
    // Continuing legacy behavior; if unspecified, we treat this as ingress.
    operation_name_ = Tracing::OperationName::Ingress;
    break;
  case envoy::config::core::v3::INBOUND:
    operation_name_ = Tracing::OperationName::Ingress;
    break;
  case envoy::config::core::v3::OUTBOUND:
    operation_name_ = Tracing::OperationName::Egress;
    break;
  }

  spawn_upstream_span_ =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(tracing_config, spawn_upstream_span, false);

  no_context_propagation_ = tracing_config.no_context_propagation();

  for (const auto& tag : tracing_config.custom_tags()) {
    custom_tags_.emplace(tag.tag(),
                         Tracing::CustomTagUtility::createCustomTag(tag, command_parsers));
  }

  client_sampling_.set_numerator(
      tracing_config.has_client_sampling() ? tracing_config.client_sampling().value() : 100);

  // TODO: Random sampling historically was an integer and default to out of 10,000. We should
  // deprecate that and move to a straight fractional percent config.
  uint64_t random_sampling_numerator{PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
      tracing_config, random_sampling, 10000, 10000)};
  random_sampling_.set_numerator(random_sampling_numerator);
  random_sampling_.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  uint64_t overall_sampling_numerator{PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
      tracing_config, overall_sampling, 10000, 10000)};
  overall_sampling_.set_numerator(overall_sampling_numerator);
  overall_sampling_.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  verbose_ = tracing_config.verbose();
  max_path_tag_length_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(tracing_config, max_path_tag_length,
                                                         Tracing::DefaultMaxPathTagLength);

  if (!tracing_config.operation().empty()) {
    auto operation =
        Formatter::FormatterImpl::create(tracing_config.operation(), true, command_parsers);
    THROW_IF_NOT_OK_REF(operation.status());
    operation_ = std::move(operation.value());
  }

  if (!tracing_config.upstream_operation().empty()) {
    auto operation = Formatter::FormatterImpl::create(tracing_config.upstream_operation(), true,
                                                      command_parsers);
    THROW_IF_NOT_OK_REF(operation.status());
    upstream_operation_ = std::move(operation.value());
  }
}

} // namespace Tracing
} // namespace Envoy
