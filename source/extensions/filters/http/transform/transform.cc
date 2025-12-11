#include "source/extensions/filters/http/transform/transform.h"

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/transform/v3/transform.pb.h"

#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_utility.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {

absl::optional<std::string> BodyFormatterProvider::format(const Formatter::Context& context,
                                                          const StreamInfo::StreamInfo&) const {
  const auto extension = context.typedExtension<BodyContextExtension>();
  if (!extension.has_value()) {
    return absl::nullopt;
  }
  const auto& body = request_body_ ? extension->request_body : extension->response_body;
  const auto& value = Config::Metadata::structValue(body, path_);
  if (value.kind_case() == Protobuf::Value::kNullValue ||
      value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
    return absl::nullopt;
  }
  if (value.kind_case() == Protobuf::Value::kStringValue) {
    return value.string_value();
  }
  std::string str;
  Json::Utility::appendValueToString(value, str);
  return str;
}

Protobuf::Value BodyFormatterProvider::formatValue(const Formatter::Context& context,
                                                   const StreamInfo::StreamInfo&) const {
  const auto extension = context.typedExtension<BodyContextExtension>();
  if (!extension.has_value()) {
    return Protobuf::Value::default_instance();
  }
  const auto& body = request_body_ ? extension->request_body : extension->response_body;
  return Config::Metadata::structValue(body, path_);
}

/**
 * CommandParser for BodyFormatterProvider.
 */
class BodyFormatterCommandParser : public Formatter::CommandParser {
public:
  BodyFormatterCommandParser() = default;

  Formatter::FormatterProviderPtr parse(absl::string_view command, absl::string_view command_arg,
                                        absl::optional<size_t>) const override {

    if (command == "REQUEST_BODY") {
      return std::make_unique<BodyFormatterProvider>(command_arg, true);
    }
    if (command == "RESPONSE_BODY") {
      return std::make_unique<BodyFormatterProvider>(command_arg, false);
    }
    return nullptr;
  }
};

const std::vector<Formatter::CommandParserPtr>& bodyCommandParsers() {
  static const std::vector<Formatter::CommandParserPtr> instance = []() {
    std::vector<Formatter::CommandParserPtr> v;
    v.emplace_back(std::make_unique<BodyFormatterCommandParser>());
    return v;
  }();
  return instance;
}

Transformation::Transformation(const ProtoTransformation& config,
                               Server::Configuration::ServerFactoryContext& context,
                               absl::Status& creation_status) {
  if (config.has_body_transformation()) {
    if (config.body_transformation().has_body_format()) {
      std::vector<Formatter::CommandParserPtr> v;
      v.emplace_back(std::make_unique<BodyFormatterCommandParser>());
      Server::GenericFactoryContextImpl generic_context(context,
                                                        context.messageValidationVisitor());
      auto formatter_or = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          config.body_transformation().body_format(), generic_context, std::move(v));
      SET_AND_RETURN_IF_NOT_OK(formatter_or.status(), creation_status);
      body_formatter_ = std::move(formatter_or.value());
      content_type_ = config.body_transformation().body_format().content_type();

      merge_format_string_ =
          config.body_transformation().action() ==
          envoy::extensions::filters::http::transform::v3::BodyTransformation::MERGE;
    }
  }

  if (config.headers_mutations().size() > 0) {
    auto mutations_or =
        Http::HeaderMutations::create(config.headers_mutations(), context, bodyCommandParsers());
    SET_AND_RETURN_IF_NOT_OK(mutations_or.status(), creation_status);
    headers_mutations_ = std::move(mutations_or.value());
  }
}

TransformConfig::TransformConfig(const ProtoConfig& config,
                                 Server::Configuration::ServerFactoryContext& context,
                                 absl::Status& creation_status)
    : clear_route_cache_(config.clear_route_cache()),
      clear_cluster_cache_(config.clear_cluster_cache()) {
  if (config.has_request_transformation()) {
    request_transformation_.emplace(config.request_transformation(), context, creation_status);
  }
  if (config.has_response_transformation()) {
    response_transformation_.emplace(config.response_transformation(), context, creation_status);
  }

  if (clear_cluster_cache_ && clear_route_cache_) {
    creation_status = absl::InvalidArgumentError(
        "Only one of clear_cluster_cache and clear_route_cache can be set to true");
  }
}

void TransformFilter::maybeInitializeRouteConfigs(Http::StreamFilterCallbacks* callbacks) {
  // Ensure that route configs are initialized only once and the same route configs are used
  // for both decoding and encoding paths.
  // An independent flag is used to ensure even at the case where the route configs is empty,
  // we still won't try to initialize it again.
  if (route_configs_initialized_) {
    return;
  }
  route_configs_initialized_ = true;

  // Traverse through all route configs to retrieve all available header mutations.
  // `getAllPerFilterConfig` returns in ascending order of specificity (i.e., route table
  // first, then virtual host, then per route).
  auto route_config = Http::Utility::resolveMostSpecificPerFilterConfig<TransformConfig>(callbacks);
  if (route_config != nullptr) {
    effective_config_ = route_config;
  } else {
    effective_config_ = config_.get();
  }
}

Http::FilterHeadersStatus TransformFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                         bool end_stream) {
  // Skip transformation for headers only requests or non-JSON requests.
  if (end_stream || !absl::StrContains(headers.getContentTypeValue(),
                                       Http::Headers::get().ContentTypeValues.Json)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Initialize effective route configs if not done yet.
  maybeInitializeRouteConfigs(decoder_callbacks_);
  ASSERT(effective_config_ != nullptr);

  if (!effective_config_->requestTransformation().has_value()) {
    // No request transform configured, continue.
    return Http::FilterHeadersStatus::Continue;
  }

  // No request transform configured, continue.
  decoding_enabled_ = true;
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus TransformFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (!decoding_enabled_) {
    return Http::FilterDataStatus::Continue;
  }
  if (end_stream) {
    decoder_callbacks_->addDecodedData(data, true);
    handleCompleteRequestBody();
    return Http::FilterDataStatus::Continue;
  }
  return Http::FilterDataStatus::StopIterationAndBuffer;
}

Http::FilterTrailersStatus TransformFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (!decoding_enabled_) {
    return Http::FilterTrailersStatus::Continue;
  }
  handleCompleteRequestBody();
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus TransformFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                         bool end_stream) {
  if (saw_local_reply_) {
    // If this is a local reply, we should not apply response transformation.
    return Http::FilterHeadersStatus::Continue;
  }

  // Skip transformation for headers only responses or non-JSON responses.
  if (end_stream || !absl::StrContains(headers.getContentTypeValue(),
                                       Http::Headers::get().ContentTypeValues.Json)) {
    return Http::FilterHeadersStatus::Continue;
  }

  maybeInitializeRouteConfigs(encoder_callbacks_);
  ASSERT(effective_config_ != nullptr);

  if (!effective_config_->responseTransformation().has_value()) {
    // No response transform configured, continue.
    return Http::FilterHeadersStatus::Continue;
  }

  encoding_enabled_ = true;
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus TransformFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (saw_local_reply_) {
    // If this is a local reply, we should not apply response transformation.
    return Http::FilterDataStatus::Continue;
  }

  if (!encoding_enabled_) {
    return Http::FilterDataStatus::Continue;
  }
  if (end_stream) {
    encoder_callbacks_->addEncodedData(data, true);
    handleCompleteResponseBody();
    return Http::FilterDataStatus::Continue;
  }
  return Http::FilterDataStatus::StopIterationAndBuffer;
}

Http::FilterTrailersStatus TransformFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (saw_local_reply_) {
    // If this is a local reply, we should not apply response transformation.
    return Http::FilterTrailersStatus::Continue;
  }

  if (!encoding_enabled_) {
    return Http::FilterTrailersStatus::Continue;
  }
  handleCompleteResponseBody();
  return Http::FilterTrailersStatus::Continue;
}

TransformFilter::TransformResult TransformFilter::handleCompleteBody(
    const Transformation& transformation, const Formatter::Context& context,
    const Buffer::Instance& body_buffer, Protobuf::Struct& body_struct, Http::HeaderMap& headers) {
  uint64_t body_size = body_buffer.length();
  absl::Status status = MessageUtil::loadFromJsonNoThrow(body_buffer.toString(), body_struct);
  if (!status.ok()) {
    ENVOY_LOG(info, "Failed to parse request/response body as JSON: {}", status.message());
    // Failed to parse the body, continue without transformation.
    return {};
  }

  std::string new_buffer;
  Protobuf::Struct new_struct;
  bool transform_buffer = false;
  bool transform_header = false;

  // Transform the body if configured and validate the result is valid JSON if patch
  // mode is enabled.
  if (transformation.bodyFormatter().has_value()) {
    new_buffer = transformation.bodyFormatter()->format(context, decoder_callbacks_->streamInfo());
    if (transformation.mergeFormatString()) {
      if (auto s = MessageUtil::loadFromJsonNoThrow(new_buffer, new_struct); !s.ok()) {
        ENVOY_LOG(error, "Failed to parse transformed body as JSON: {}", s.message());
        return {};
      }
    }
    transform_buffer = true;
  }

  // Now there should no longer be any body transformation errors.

  // Apply header mutations first if configured.
  if (transformation.headerMutations().has_value()) {
    transformation.headerMutations()->evaluateHeaders(headers, context,
                                                      decoder_callbacks_->streamInfo());
    transform_header = true;
  }

  // Merge the new struct into the original response body if in merge mode.
  if (transformation.bodyFormatter().has_value()) {
    if (transformation.mergeFormatString()) {
      body_struct.MergeFrom(new_struct);
      body_size += new_buffer.size();
      new_buffer.clear();
      new_buffer.reserve(body_size + 64); // 64 bytes for safety margin.
      Json::Utility::appendStructToString(body_struct, new_buffer);
    }
  }

  return {std::move(new_buffer), transform_buffer, transform_header};
}

void TransformFilter::handleCompleteRequestBody() {
  ASSERT(decoding_enabled_);
  const auto* decoding_buffer = decoder_callbacks_->decodingBuffer();
  if (decoding_buffer == nullptr) {
    // No body to transform and do nothing.
    // TODO(wbpcode): maybe we can support adding new body even when there is no original
    // body in the future. But for now I cannot figure out a meaningful use case.
    return;
  }

  const auto transformation = effective_config_->requestTransformation();
  ASSERT(transformation.has_value());
  Http::RequestHeaderMapOptRef headers = decoder_callbacks_->requestHeaders();
  ASSERT(headers.has_value());
  Formatter::Context formatter_context(headers.ptr());
  formatter_context.setExtension(body_extension_);

  const auto result = handleCompleteBody(*transformation, formatter_context, *decoding_buffer,
                                         body_extension_.request_body, *headers);

  if (result.transform_buffer || result.transform_header) {
    config_->stats().rq_transformed_.inc();
  }

  if (result.transform_buffer) {
    headers->removeContentLength();
    if (!transformation->contentType().empty()) {
      headers->setContentType(transformation->contentType());
    }
    decoder_callbacks_->modifyDecodingBuffer([&result](Buffer::Instance& data) {
      data.drain(data.length());
      data.add(result.buffer);
    });
  }

  if (result.transform_header) {
    if (auto cb = decoder_callbacks_->downstreamCallbacks(); cb.has_value()) {
      if (effective_config_->clearClusterCache()) {
        cb->refreshRouteCluster();
      }
      if (effective_config_->clearRouteCache()) {
        cb->clearRouteCache();
      }
    }
  }
}

void TransformFilter::handleCompleteResponseBody() {
  ASSERT(encoding_enabled_);
  const auto* encoding_buffer = encoder_callbacks_->encodingBuffer();
  if (encoding_buffer == nullptr) {
    // No body to transform and do nothing.
    // TODO(wbpcode): maybe we can support adding new body even when there is no original
    // body in the future. But for now I cannot figure out a meaningful use case.
    return;
  }
  const auto transformation = effective_config_->responseTransformation();
  ASSERT(transformation.has_value());
  Http::ResponseHeaderMapOptRef headers = encoder_callbacks_->responseHeaders();
  ASSERT(headers.has_value());
  Formatter::Context formatter_context(decoder_callbacks_->requestHeaders().ptr(), headers.ptr());
  formatter_context.setExtension(body_extension_);

  const auto result = handleCompleteBody(*transformation, formatter_context, *encoding_buffer,
                                         body_extension_.response_body, *headers);

  if (result.transform_buffer || result.transform_header) {
    config_->stats().rs_transformed_.inc();
  }

  if (result.transform_buffer) {
    headers->removeContentLength();
    if (!transformation->contentType().empty()) {
      headers->setContentType(transformation->contentType());
    }
    encoder_callbacks_->modifyEncodingBuffer([&result](Buffer::Instance& data) {
      data.drain(data.length());
      data.add(result.buffer);
    });
  }
}

} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
