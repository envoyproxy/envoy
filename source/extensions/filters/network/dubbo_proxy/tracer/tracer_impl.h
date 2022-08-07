#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/tracing/trace_config.h"
#include "envoy/tracing/http_tracer.h"

#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/http/conn_manager_config.h"
#include "source/common/tracing/http_tracer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Tracer {

class Tracer : public Tcp::ConnectionPool::UpstreamCallbacks,
               public DubboFilters::CodecFilter {
public:
  Tracer() = default;
  ~Tracer() override = default;

  // DubboFilters::DecoderFilter
  void onDestroy() override;
  void setDecoderFilterCallbacks(DubboFilters::DecoderFilterCallbacks& callbacks) override;

  FilterStatus onMessageDecoded(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) override;

  // DubboFilter::EncoderFilter
  void setEncoderFilterCallbacks(DubboFilters::EncoderFilterCallbacks& callbacks) override;
  FilterStatus onMessageEncoded(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}
};

class TracerConfigImpl : public Config,
                         public Logger::Loggable<Logger::Id::dubbo> {
public:
  TracerConfigImpl(Http::TracingConnectionManagerConfigPtr&& tracing_config,
                   Tracing::HttpTracerSharedPtr tracer)
      : tracing_config_(std::move(tracing_config)), tracer_(tracer) {}
  Tracing::HttpTracerSharedPtr tracer() const override;
  const Http::TracingConnectionManagerConfig* tracingConfig() const override;

  // Tracing::Config
  Tracing::OperationName operationName() const override;
  const Tracing::CustomTagMap* customTags() const override;
  bool verbose() const override;
  uint32_t maxPathTagLength() const override;

private:
  Http::TracingConnectionManagerConfigPtr tracing_config_;
  Tracing::HttpTracerSharedPtr tracer_;
};

class NullTracerConfig : public Config {
public:
  Tracing::HttpTracerSharedPtr tracer() const override { return nullptr; }
  const Http::TracingConnectionManagerConfig* tracingConfig() const override {return nullptr; }

  // Tracing::Config
  Tracing::OperationName operationName() const override { return Tracing::OperationName(); }
  const Tracing::CustomTagMap* customTags() const override { return nullptr; }
  bool verbose() const override { return false; }
  uint32_t maxPathTagLength() const override { return 0UL;}
};

using TracerConfigImplPtr = std::unique_ptr<Config>;


} // namespace Tracer
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
