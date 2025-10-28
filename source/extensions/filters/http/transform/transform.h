#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/transform/v3/transform.pb.h"
#include "envoy/http/query_params.h"

#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_mutation.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {

using ProtoConfig = envoy::extensions::filters::http::transform::v3::TransformConfig;
using ProtoTransformation = envoy::extensions::filters::http::transform::v3::Transformation;

/**
 * BodyContextExtension which holds request and response body as Struct. The substitution
 * formatter can access the body via this extension.
 */
class BodyContextExtension : public Formatter::Context::Extension {
public:
  Protobuf::Struct request_body;
  Protobuf::Struct response_body;
};

/**
 * All stats for the Stateful Session filter. @see stats_macros.h
 */
#define ALL_STATEFUL_SESSION_FILTER_STATS(COUNTER)                                                 \
  COUNTER(rq_transformed)                                                                          \
  COUNTER(rs_transformed)

/**
 * Wrapper struct for Transform filter stats. @see stats_macros.h
 */
struct TransformFilterStats {
  ALL_STATEFUL_SESSION_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * BodyFormatterProvider implements FormatterProvider to extract values from request or response
 * body stored in BodyContextExtension.
 */
class BodyFormatterProvider : public Formatter::FormatterProvider {
public:
  BodyFormatterProvider(absl::string_view path, bool request_body)
      : path_(absl::StrSplit(path, ':')), request_body_(request_body) {}

  // FormatterProvider
  absl::optional<std::string> format(const Formatter::Context& context,
                                     const StreamInfo::StreamInfo&) const override;
  Protobuf::Value formatValue(const Formatter::Context& context,
                              const StreamInfo::StreamInfo&) const override;

private:
  const std::vector<std::string> path_;
  const bool request_body_{};
};

/**
 * Transformation holds the configuration for request or response transformation.
 */
class Transformation {
public:
  Transformation(const ProtoTransformation& config,
                 Server::Configuration::ServerFactoryContext& context,
                 absl::Status& creation_status);

  /**
   * Get the header mutations configuration if any.
   */
  OptRef<const Http::HeaderMutations> headerMutations() const {
    return makeOptRefFromPtr(headers_mutations_.get());
  }

  /**
   * Get the body formatter configuration if any.
   */
  OptRef<const Formatter::Formatter> bodyFormatter() const {
    return makeOptRefFromPtr(body_formatter_.get());
  }
  /**
   * Whether to patch the body using the patch_format_string field in the config or
   * completely replace the body using the body_format_string field in the config.
   */
  bool mergeFormatString() const { return merge_format_string_; }

  /**
   * Get the content type to set in the Content-Type header if body transformation is
   * performed and content_type is specified in the body_format_string config.
   */
  absl::string_view contentType() const { return content_type_; }

private:
  Formatter::FormatterPtr body_formatter_;
  std::string content_type_;
  std::unique_ptr<Http::HeaderMutations> headers_mutations_;
  // TODO(wbpcode): consider enum if more modes are added in the future.
  bool merge_format_string_{};
};

/**
 * Transform configuration for the transform filter.
 */
class TransformConfig : public Router::RouteSpecificFilterConfig {
public:
  TransformConfig(const ProtoConfig& config, Server::Configuration::ServerFactoryContext& context,
                  absl::Status& creation_status);

  OptRef<const Transformation> requestTransformation() const {
    if (request_transformation_.has_value()) {
      return request_transformation_.value();
    }
    return {};
  }
  OptRef<const Transformation> responseTransformation() const {
    if (response_transformation_.has_value()) {
      return response_transformation_.value();
    }
    return {};
  }

  bool clearRouteCache() const { return clear_route_cache_; }
  bool clearClusterCache() const { return clear_cluster_cache_; }

private:
  absl::optional<Transformation> request_transformation_;
  absl::optional<Transformation> response_transformation_;
  const bool clear_route_cache_{};
  const bool clear_cluster_cache_{};
};

using TransformConfigSharedPtr = std::shared_ptr<TransformConfig>;

class FilterConfig : public TransformConfig {
public:
  FilterConfig(const ProtoConfig& config, const std::string& stats_prefix,
               Server::Configuration::FactoryContext& context, absl::Status& creation_status)
      : TransformConfig(config, context.serverFactoryContext(), creation_status),
        stats_(generateStats(stats_prefix, context.scope())) {}

  TransformFilterStats& stats() { return stats_; }

private:
  TransformFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    const std::string final_prefix = prefix + ".http_transform";
    return {ALL_STATEFUL_SESSION_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }
  TransformFilterStats stats_;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * TransformFilter implements a HTTP filter that can transform request and response body
 * and headers.
 */
class TransformFilter : public Http::PassThroughFilter,
                        public Logger::Loggable<Logger::Id::filter> {
public:
  TransformFilter(FilterConfigSharedPtr config) : config_(std::move(config)) {
    ASSERT(config_ != nullptr);
  }

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

  Http::LocalErrorStatus onLocalReply(const LocalReplyData&) override {
    saw_local_reply_ = true;
    return Http::LocalErrorStatus::Continue;
  }

private:
  void maybeInitializeRouteConfigs(Http::StreamFilterCallbacks* callbacks);

  void handleCompleteRequestBody();
  void handleCompleteResponseBody();

  struct TransformResult {
    std::string buffer;
    bool transform_buffer{false};
    bool transform_header{false};
  };

  TransformResult handleCompleteBody(const Transformation& transformation,
                                     const Formatter::Context& context,
                                     const Buffer::Instance& body_buffer,
                                     Protobuf::Struct& body_struct, Http::HeaderMap& headers);

  BodyContextExtension body_extension_;

  FilterConfigSharedPtr config_;
  const TransformConfig* effective_config_ = nullptr;
  bool route_configs_initialized_ = false;

  bool decoding_enabled_ = false;
  bool encoding_enabled_ = false;
  bool saw_local_reply_ = false;
};

} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
