#include "test/extensions/filters/http/common/fuzz/uber_filter.h"

#include "common/config/utility.h"
#include "common/config/version_converter.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

UberFilterFuzzer::UberFilterFuzzer() : async_request_{&cluster_manager_.async_client_} {
  // This is a decoder filter.
  ON_CALL(filter_callback_, addStreamDecoderFilter(_))
      .WillByDefault(Invoke([&](Http::StreamDecoderFilterSharedPtr filter) -> void {
        decoder_filter_ = filter;
        decoder_filter_->setDecoderFilterCallbacks(decoder_callbacks_);
      }));
  // This is an encoded filter.
  ON_CALL(filter_callback_, addStreamEncoderFilter(_))
      .WillByDefault(Invoke([&](Http::StreamEncoderFilterSharedPtr filter) -> void {
        encoder_filter_ = filter;
        encoder_filter_->setEncoderFilterCallbacks(encoder_callbacks_);
      }));
  // This is a decoder and encoder filter.
  ON_CALL(filter_callback_, addStreamFilter(_))
      .WillByDefault(Invoke([&](Http::StreamFilterSharedPtr filter) -> void {
        decoder_filter_ = filter;
        decoder_filter_->setDecoderFilterCallbacks(decoder_callbacks_);
        encoder_filter_ = filter;
        encoder_filter_->setEncoderFilterCallbacks(encoder_callbacks_);
      }));
  // This filter supports access logging.
  ON_CALL(filter_callback_, addAccessLogHandler(_))
      .WillByDefault(
          Invoke([&](AccessLog::InstanceSharedPtr handler) -> void { access_logger_ = handler; }));
  // This handles stopping execution after a direct response is sent.
  ON_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillByDefault(
          Invoke([this](Http::Code code, absl::string_view body,
                        std::function<void(Http::ResponseHeaderMap & headers)> modify_headers,
                        const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                        absl::string_view details) {
            enabled_ = false;
            decoder_callbacks_.sendLocalReply_(code, body, modify_headers, grpc_status, details);
          }));
  // Set expectations for particular filters that may get fuzzed.
  perFilterSetup();
}

std::vector<std::string> UberFilterFuzzer::parseHttpData(const test::fuzz::HttpData& data) {
  std::vector<std::string> data_chunks;

  if (data.has_http_body()) {
    data_chunks.reserve(data.http_body().data_size());
    for (const std::string& http_data : data.http_body().data()) {
      data_chunks.push_back(http_data);
    }
  } else if (data.has_proto_body()) {
    const std::string serialized = data.proto_body().message().value();
    data_chunks = absl::StrSplit(serialized, absl::ByLength(data.proto_body().chunk_size()));
  }

  return data_chunks;
}

template <class FilterType>
void UberFilterFuzzer::runData(FilterType* filter, const test::fuzz::HttpData& data) {
  bool end_stream = false;
  enabled_ = true;
  if (data.body_case() == test::fuzz::HttpData::BODY_NOT_SET && !data.has_trailers()) {
    end_stream = true;
  }
  const auto& headersStatus = sendHeaders(filter, data, end_stream);
  ENVOY_LOG_MISC(debug, "Finished with FilterHeadersStatus: {}", headersStatus);
  if ((headersStatus != Http::FilterHeadersStatus::Continue &&
       headersStatus != Http::FilterHeadersStatus::StopIteration) ||
      !enabled_) {
    return;
  }

  const std::vector<std::string> data_chunks = parseHttpData(data);
  for (size_t i = 0; i < data_chunks.size(); i++) {
    if (!data.has_trailers() && i == data_chunks.size() - 1) {
      end_stream = true;
    }
    Buffer::OwnedImpl buffer(data_chunks[i]);
    const auto& dataStatus = sendData(filter, buffer, end_stream);
    ENVOY_LOG_MISC(debug, "Finished with FilterDataStatus: {}", dataStatus);
    if (dataStatus != Http::FilterDataStatus::Continue || !enabled_) {
      return;
    }
  }

  if (data.has_trailers() && enabled_) {
    sendTrailers(filter, data);
  }
}

template <>
Http::FilterHeadersStatus UberFilterFuzzer::sendHeaders(Http::StreamDecoderFilter* filter,
                                                        const test::fuzz::HttpData& data,
                                                        bool end_stream) {
  request_headers_ = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(data.headers());
  if (request_headers_.Path() == nullptr) {
    request_headers_.setPath("/foo");
  }
  if (request_headers_.Method() == nullptr) {
    request_headers_.setMethod("GET");
  }
  if (request_headers_.Host() == nullptr) {
    request_headers_.setHost("foo.com");
  }

  ENVOY_LOG_MISC(debug, "Decoding headers (end_stream={}):\n{} ", end_stream, request_headers_);
  return filter->decodeHeaders(request_headers_, end_stream);
}

template <>
Http::FilterHeadersStatus UberFilterFuzzer::sendHeaders(Http::StreamEncoderFilter* filter,
                                                        const test::fuzz::HttpData& data,
                                                        bool end_stream) {
  response_headers_ = Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(data.headers());

  // Status must be a valid unsigned long. If not set, the utility function below will throw
  // an exception on the data path of some filters. This should never happen in production, so catch
  // the exception and set to a default value.
  try {
    (void)Http::Utility::getResponseStatus(response_headers_);
  } catch (const Http::CodecClientException& e) {
    response_headers_.setStatus(200);
  }

  ENVOY_LOG_MISC(debug, "Encoding headers (end_stream={}):\n{} ", end_stream, response_headers_);
  Http::FilterHeadersStatus status = filter->encodeHeaders(response_headers_, end_stream);
  if (end_stream) {
    filter->encodeComplete();
  }
  return status;
}

template <>
Http::FilterDataStatus UberFilterFuzzer::sendData(Http::StreamDecoderFilter* filter,
                                                  Buffer::Instance& buffer, bool end_stream) {
  ENVOY_LOG_MISC(debug, "Decoding data (end_stream={}): {} ", end_stream, buffer.toString());
  return filter->decodeData(buffer, end_stream);
}

template <>
Http::FilterDataStatus UberFilterFuzzer::sendData(Http::StreamEncoderFilter* filter,
                                                  Buffer::Instance& buffer, bool end_stream) {
  ENVOY_LOG_MISC(debug, "Encoding data (end_stream={}): {} ", end_stream, buffer.toString());
  Http::FilterDataStatus status = filter->encodeData(buffer, end_stream);
  if (end_stream) {
    filter->encodeComplete();
  }
  return status;
}

template <>
void UberFilterFuzzer::sendTrailers(Http::StreamDecoderFilter* filter,
                                    const test::fuzz::HttpData& data) {
  request_trailers_ = Fuzz::fromHeaders<Http::TestRequestTrailerMapImpl>(data.trailers());
  ENVOY_LOG_MISC(debug, "Decoding trailers:\n{} ", request_trailers_);
  filter->decodeTrailers(request_trailers_);
}

template <>
void UberFilterFuzzer::sendTrailers(Http::StreamEncoderFilter* filter,
                                    const test::fuzz::HttpData& data) {
  response_trailers_ = Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(data.trailers());
  ENVOY_LOG_MISC(debug, "Encoding trailers:\n{} ", response_trailers_);
  filter->encodeTrailers(response_trailers_);
  filter->encodeComplete();
}

void UberFilterFuzzer::accessLog(AccessLog::Instance* access_logger,
                                 const StreamInfo::StreamInfo& stream_info) {
  ENVOY_LOG_MISC(debug, "Access logging");
  access_logger->log(&request_headers_, &response_headers_, &response_trailers_, stream_info);
}

void UberFilterFuzzer::fuzz(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
        proto_config,
    const test::fuzz::HttpData& downstream_data, const test::fuzz::HttpData& upstream_data) {
  try {
    // Try to create the filter. Exit early if the config is invalid or violates PGV constraints.
    ENVOY_LOG_MISC(info, "filter name {}", proto_config.name());
    auto& factory = Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::NamedHttpFilterConfigFactory>(proto_config.name());
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, factory_context_.messageValidationVisitor(), factory);
    // Clean-up config with filter-specific logic.
    cleanFuzzedConfig(proto_config.name(), message.get());
    cb_ = factory.createFilterFactoryFromProto(*message, "stats", factory_context_);
    cb_(filter_callback_);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Controlled exception {}", e.what());
    return;
  }

  // Data path should not throw exceptions.
  if (decoder_filter_ != nullptr) {
    runData(decoder_filter_.get(), downstream_data);
  }
  if (encoder_filter_ != nullptr) {
    runData(encoder_filter_.get(), upstream_data);
  }
  if (access_logger_ != nullptr) {
    accessLog(access_logger_.get(), stream_info_);
  }

  reset();
}

void UberFilterFuzzer::reset() {
  if (decoder_filter_ != nullptr) {
    decoder_filter_->onDestroy();
  }
  decoder_filter_.reset();

  if (encoder_filter_ != nullptr) {
    encoder_filter_->onDestroy();
  }
  encoder_filter_.reset();

  access_logger_.reset();
  request_headers_.clear();
  response_headers_.clear();
  request_trailers_.clear();
  response_trailers_.clear();
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
