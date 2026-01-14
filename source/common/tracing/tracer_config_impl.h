#pragma once

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/tracer_config.h"

#include "source/common/tracing/custom_tag_impl.h"

namespace Envoy {
namespace Tracing {

using ConnectionManagerTracingConfigProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager_Tracing;

class TracerFactoryContextImpl : public Server::Configuration::TracerFactoryContext {
public:
  TracerFactoryContextImpl(Server::Configuration::ServerFactoryContext& server_factory_context,
                           ProtobufMessage::ValidationVisitor& validation_visitor)
      : server_factory_context_(server_factory_context), validation_visitor_(validation_visitor) {}
  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return server_factory_context_;
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }

private:
  Server::Configuration::ServerFactoryContext& server_factory_context_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

class ConnectionManagerTracingConfig {
public:
  ConnectionManagerTracingConfig(envoy::config::core::v3::TrafficDirection traffic_direction,
                                 const ConnectionManagerTracingConfigProto& tracing_config,
                                 const Formatter::CommandParserPtrVector& command_parsers = {}) {

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
  }

  ConnectionManagerTracingConfig(Tracing::OperationName operation_name,
                                 Tracing::CustomTagMap custom_tags,
                                 envoy::type::v3::FractionalPercent client_sampling,
                                 envoy::type::v3::FractionalPercent random_sampling,
                                 envoy::type::v3::FractionalPercent overall_sampling, bool verbose,
                                 uint32_t max_path_tag_length)
      : operation_name_(operation_name), custom_tags_(std::move(custom_tags)),
        client_sampling_(std::move(client_sampling)), random_sampling_(std::move(random_sampling)),
        overall_sampling_(std::move(overall_sampling)), verbose_(verbose),
        max_path_tag_length_(max_path_tag_length) {}

  ConnectionManagerTracingConfig() = default;

  const envoy::type::v3::FractionalPercent& getClientSampling() const { return client_sampling_; }
  const envoy::type::v3::FractionalPercent& getRandomSampling() const { return random_sampling_; }
  const envoy::type::v3::FractionalPercent& getOverallSampling() const { return overall_sampling_; }
  const Tracing::CustomTagMap& getCustomTags() const { return custom_tags_; }

  Tracing::OperationName operationName() const { return operation_name_; }
  bool verbose() const { return verbose_; }
  uint32_t maxPathTagLength() const { return max_path_tag_length_; }
  bool spawnUpstreamSpan() const { return spawn_upstream_span_; }

  // TODO(wbpcode): keep this field be public for compatibility. Then the HCM needn't change much
  // code to use this config.
  Tracing::OperationName operation_name_{};
  Tracing::CustomTagMap custom_tags_;
  envoy::type::v3::FractionalPercent client_sampling_;
  envoy::type::v3::FractionalPercent random_sampling_;
  envoy::type::v3::FractionalPercent overall_sampling_;
  bool verbose_{};
  uint32_t max_path_tag_length_{};
  bool spawn_upstream_span_{};
};

using ConnectionManagerTracingConfigPtr = std::unique_ptr<ConnectionManagerTracingConfig>;

} // namespace Tracing
} // namespace Envoy
