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
using ProtoTransform = envoy::extensions::filters::http::transform::v3::Transform;

/**
 * BodyContextExtension which holds request and response body as Struct. The substitution
 * formatter can access the body via this extension.
 */
class BodyContextExtension : public Formatter::Context::Extension {
public:
  google::protobuf::Struct request_body;
  google::protobuf::Struct response_body;
};

/**
 * Transform holds the configuration for request or response transformation.
 */
class Transform {
public:
  Transform(const ProtoTransform& config, Server::Configuration::ServerFactoryContext& context,
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
  bool patchFormatString() const { return patch_format_string_; }

  /**
   * Get the content type to set in the Content-Type header if body transformation is
   * performed and content_type is specified in the body_format_string config.
   */
  absl::string_view contentType() const { return content_type_; }

private:
  Formatter::FormatterPtr body_formatter_;
  std::string content_type_;
  std::unique_ptr<Http::HeaderMutations> headers_mutations_;
  const bool patch_format_string_{};
};

/**
 * Transform configuration for the transform filter.
 */
class TransformConfig : public Router::RouteSpecificFilterConfig {
public:
  TransformConfig(const ProtoConfig& config, Server::Configuration::ServerFactoryContext& context,
                  absl::Status& creation_status);

  OptRef<const Transform> requestTransform() const {
    if (request_transform_.has_value()) {
      return request_transform_.value();
    }
    return {};
  }
  OptRef<const Transform> responseTransform() const {
    if (response_transform_.has_value()) {
      return response_transform_.value();
    }
    return {};
  }

  bool clearRouteCache() const { return clear_route_cache_; }
  bool clearClusterCache() const { return clear_cluster_cache_; }

private:
  absl::optional<Transform> request_transform_;
  absl::optional<Transform> response_transform_;
  const bool clear_route_cache_{};
  const bool clear_cluster_cache_{};
};

using TransformConfigSharedPtr = std::shared_ptr<TransformConfig>;

/**
 * TransformFilter implements a HTTP filter that can transform request and response body
 * and headers.
 */
class TransformFilter : public Http::PassThroughFilter,
                        public Logger::Loggable<Logger::Id::filter> {
public:
  TransformFilter(TransformConfigSharedPtr config) : config_(std::move(config)) {
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

  absl::optional<std::string> handleCompleteBody(const Transform& transform,
                                                 const Formatter::Context& context,
                                                 const Buffer::Instance& body_buffer,
                                                 google::protobuf::Struct& body_struct,
                                                 Http::HeaderMap& headers);

  BodyContextExtension body_extension_;

  TransformConfigSharedPtr config_;
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
