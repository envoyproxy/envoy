#pragma once

#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/metadata/v3/metadata.pb.h"
#include "envoy/type/tracing/v3/custom_tag.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/config/metadata.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/json/json_loader.h"
#include "source/common/tracing/common_values.h"
#include "source/common/tracing/null_span_impl.h"

namespace Envoy {
namespace Tracing {

class HttpTracerUtility {
public:
  /**
   * Get string representation of the operation.
   * @param operation name to convert.
   * @return string representation of the operation.
   */
  static const std::string& toString(OperationName operation_name);

  /**
   * Request might be traceable if the request ID is traceable or we do sampling tracing.
   * Note: there is a global switch which turns off tracing completely on server side.
   *
   * @return decision if request is traceable or not and Reason why.
   **/
  static Decision shouldTraceRequest(const StreamInfo::StreamInfo& stream_info);

  /**
   * Adds information obtained from the downstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeDownstreamSpan(Span& span, const Http::RequestHeaderMap* request_headers,
                                     const Http::ResponseHeaderMap* response_headers,
                                     const Http::ResponseTrailerMap* response_trailers,
                                     const StreamInfo::StreamInfo& stream_info,
                                     const Config& tracing_config);

  /**
   * Adds information obtained from the upstream request headers as tags to the active span.
   * Then finishes the span.
   */
  static void finalizeUpstreamSpan(Span& span, const Http::ResponseHeaderMap* response_headers,
                                   const Http::ResponseTrailerMap* response_trailers,
                                   const StreamInfo::StreamInfo& stream_info,
                                   const Config& tracing_config);

  /**
   * Create a custom tag according to the configuration.
   * @param tag a tracing custom tag configuration.
   */
  static CustomTagConstSharedPtr createCustomTag(const envoy::type::tracing::v3::CustomTag& tag);

private:
  static void setCommonTags(Span& span, const Http::ResponseHeaderMap* response_headers,
                            const Http::ResponseTrailerMap* response_trailers,
                            const StreamInfo::StreamInfo& stream_info,
                            const Config& tracing_config);

  static const std::string IngressOperation;
  static const std::string EgressOperation;
};

class EgressConfigImpl : public Config {
public:
  // Tracing::Config
  Tracing::OperationName operationName() const override { return Tracing::OperationName::Egress; }
  const CustomTagMap* customTags() const override { return nullptr; }
  bool verbose() const override { return false; }
  uint32_t maxPathTagLength() const override { return Tracing::DefaultMaxPathTagLength; }
};

using EgressConfig = ConstSingleton<EgressConfigImpl>;

class HttpNullTracer : public HttpTracer {
public:
  // Tracing::HttpTracer
  SpanPtr startSpan(const Config&, Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                    const Tracing::Decision) override {
    return SpanPtr{new NullSpan()};
  }
};

class HttpTracerImpl : public HttpTracer {
public:
  HttpTracerImpl(DriverSharedPtr driver, const LocalInfo::LocalInfo& local_info);

  // Tracing::HttpTracer
  SpanPtr startSpan(const Config& config, Http::RequestHeaderMap& request_headers,
                    const StreamInfo::StreamInfo& stream_info,
                    const Tracing::Decision tracing_decision) override;

  DriverSharedPtr driverForTest() const { return driver_; }

private:
  DriverSharedPtr driver_;
  const LocalInfo::LocalInfo& local_info_;
};

class CustomTagBase : public CustomTag {
public:
  explicit CustomTagBase(const std::string& tag) : tag_(tag) {}
  absl::string_view tag() const override { return tag_; }
  void apply(Span& span, const CustomTagContext& ctx) const override;

  virtual absl::string_view value(const CustomTagContext& ctx) const PURE;

protected:
  const std::string tag_;
};

class LiteralCustomTag : public CustomTagBase {
public:
  LiteralCustomTag(const std::string& tag,
                   const envoy::type::tracing::v3::CustomTag::Literal& literal)
      : CustomTagBase(tag), value_(literal.value()) {}
  absl::string_view value(const CustomTagContext&) const override { return value_; }

private:
  const std::string value_;
};

class EnvironmentCustomTag : public CustomTagBase {
public:
  EnvironmentCustomTag(const std::string& tag,
                       const envoy::type::tracing::v3::CustomTag::Environment& environment);
  absl::string_view value(const CustomTagContext&) const override { return final_value_; }

private:
  const std::string name_;
  const std::string default_value_;
  std::string final_value_;
};

class RequestHeaderCustomTag : public CustomTagBase {
public:
  RequestHeaderCustomTag(const std::string& tag,
                         const envoy::type::tracing::v3::CustomTag::Header& request_header);
  absl::string_view value(const CustomTagContext& ctx) const override;

private:
  const Http::LowerCaseString name_;
  const std::string default_value_;
};

class MetadataCustomTag : public CustomTagBase {
public:
  MetadataCustomTag(const std::string& tag,
                    const envoy::type::tracing::v3::CustomTag::Metadata& metadata);
  void apply(Span& span, const CustomTagContext& ctx) const override;
  absl::string_view value(const CustomTagContext&) const override { return default_value_; }
  const envoy::config::core::v3::Metadata* metadata(const CustomTagContext& ctx) const;

protected:
  const envoy::type::metadata::v3::MetadataKind::KindCase kind_;
  const Envoy::Config::MetadataKey metadata_key_;
  const std::string default_value_;
};

} // namespace Tracing
} // namespace Envoy
