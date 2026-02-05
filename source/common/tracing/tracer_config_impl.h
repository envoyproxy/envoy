#pragma once

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/tracer_config.h"

#include "source/common/formatter/substitution_formatter.h"
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
                                 const Formatter::CommandParserPtrVector& command_parsers = {});

  ConnectionManagerTracingConfig(Tracing::OperationName operation_name,
                                 Tracing::CustomTagMap custom_tags,
                                 envoy::type::v3::FractionalPercent client_sampling,
                                 envoy::type::v3::FractionalPercent random_sampling,
                                 envoy::type::v3::FractionalPercent overall_sampling,
                                 Formatter::FormatterPtr operation,
                                 Formatter::FormatterPtr upstream_operation,
                                 uint32_t max_path_tag_length, bool verbose,
                                 bool no_context_propagation = false)
      : operation_name_(operation_name), custom_tags_(std::move(custom_tags)),
        client_sampling_(std::move(client_sampling)), random_sampling_(std::move(random_sampling)),
        overall_sampling_(std::move(overall_sampling)), operation_(std::move(operation)),
        upstream_operation_(std::move(upstream_operation)),
        max_path_tag_length_(max_path_tag_length), verbose_(verbose),
        no_context_propagation_(no_context_propagation) {}

  ConnectionManagerTracingConfig() = default;

  const envoy::type::v3::FractionalPercent& getClientSampling() const { return client_sampling_; }
  const envoy::type::v3::FractionalPercent& getRandomSampling() const { return random_sampling_; }
  const envoy::type::v3::FractionalPercent& getOverallSampling() const { return overall_sampling_; }
  const Tracing::CustomTagMap& getCustomTags() const { return custom_tags_; }

  Tracing::OperationName operationName() const { return operation_name_; }
  bool verbose() const { return verbose_; }
  uint32_t maxPathTagLength() const { return max_path_tag_length_; }
  bool spawnUpstreamSpan() const { return spawn_upstream_span_; }
  bool noContextPropagation() const { return no_context_propagation_; }

  // TODO(wbpcode): keep this field be public for compatibility. Then the HCM needn't change much
  // code to use this config.
  Tracing::OperationName operation_name_{};
  Tracing::CustomTagMap custom_tags_;
  envoy::type::v3::FractionalPercent client_sampling_;
  envoy::type::v3::FractionalPercent random_sampling_;
  envoy::type::v3::FractionalPercent overall_sampling_;
  Formatter::FormatterPtr operation_;
  Formatter::FormatterPtr upstream_operation_;
  uint32_t max_path_tag_length_{};
  bool verbose_{};
  bool spawn_upstream_span_{};
  bool no_context_propagation_{};
};

using ConnectionManagerTracingConfigPtr = std::unique_ptr<ConnectionManagerTracingConfig>;

} // namespace Tracing
} // namespace Envoy
