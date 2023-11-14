#pragma once

#include "envoy/http/filter.h"

#include "source/common/http/utility.h"

#include "test/fuzz/common.pb.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

// Generic library to fuzz HTTP filters.
// Usage:
//   1. Create filter and set callbacks.
//          ExampleFilter filter;
//          filter.setDecoderFilterCallbacks(decoder_callbacks);
//
//   2. Create HttpFilterFuzzer class and run decode methods. Optionally add access logging. Reset
//   fuzzer to reset state. This class can be static. All state is reset in the reset method.
//          Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
//          fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(&filter),
//                         input.downstream_request());
//          fuzzer.accessLog(static_cast<Envoy::AccessLog::Instance*>(&filter),
//                            stream_info);
//          fuzzer.reset();

class HttpFilterFuzzer {
public:
  // Instantiate HttpFilterFuzzer
  HttpFilterFuzzer() = default;

  // This executes the filter decode or encode methods with the fuzzed data.
  template <class FilterType> void runData(FilterType* filter, const test::fuzz::HttpData& data);

  // This executes the access logger with the fuzzed headers/trailers.
  void accessLog(AccessLog::Instance* access_logger, const StreamInfo::StreamInfo& stream_info) {
    ENVOY_LOG_MISC(debug, "Access logging");
    access_logger->log({&request_headers_, &response_headers_, &response_trailers_}, stream_info);
  }

  // Fuzzed headers and trailers are needed for access logging, reset the data and destroy filters.
  void reset() {
    enabled_ = true;
    encoding_finished_ = false;
    decoding_finished_ = false;
    request_headers_.clear();
    response_headers_.clear();
    request_trailers_.clear();
    response_trailers_.clear();
    encoded_trailers_.clear();
  }

  // Returns true if decoder and encoder are both finished or nonexistent.
  bool isFilterFinished() { return decoding_finished_ && encoding_finished_; }

  // Records that the filter type has finished processing.
  template <class FilterType> void finishFilter(FilterType* filter) = delete;

protected:
  // Templated functions to validate and send headers/data/trailers for decoders/encoders.
  // General functions are deleted, but templated specializations for encoders/decoders are defined
  // in the cc file.
  template <class FilterType>
  Http::FilterHeadersStatus sendHeaders(FilterType* filter, const test::fuzz::HttpData& data,
                                        bool end_stream) = delete;

  template <class FilterType>
  Http::FilterDataStatus sendData(FilterType* filter, Buffer::Instance& buffer,
                                  bool end_stream) = delete;

  template <class FilterType>
  void sendTrailers(FilterType* filter, const test::fuzz::HttpData& data) = delete;

  // This keeps track of when a filter will stop decoding due to direct responses.
  // If your filter needs to stop decoding because of a direct response, make sure you override
  // sendLocalReply to set enabled_ to false.
  bool enabled_ = true;

  bool decoding_finished_ = false;
  bool encoding_finished_ = false;

  // Headers/trailers need to be saved for the lifetime of the filter,
  // so save them as member variables.
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  Http::TestResponseTrailerMapImpl encoded_trailers_;
};

template <class FilterType>
void HttpFilterFuzzer::runData(FilterType* filter, const test::fuzz::HttpData& data) {
  bool end_stream = false;
  enabled_ = true;
  if (data.body_case() == test::fuzz::HttpData::BODY_NOT_SET && !data.has_trailers()) {
    end_stream = true;
  }
  const auto& headersStatus = sendHeaders(filter, data, end_stream);
  ENVOY_LOG_MISC(debug, "Finished with FilterHeadersStatus: {}", static_cast<int>(headersStatus));
  if ((end_stream && headersStatus == Http::FilterHeadersStatus::Continue) || !enabled_) {
    finishFilter(filter);
    return;
  }

  const std::vector<std::string> data_chunks = Fuzz::parseHttpData(data);
  for (size_t i = 0; i < data_chunks.size(); i++) {
    if (!data.has_trailers() && i == data_chunks.size() - 1) {
      end_stream = true;
    }
    Buffer::OwnedImpl buffer(data_chunks[i]);
    const auto& dataStatus = sendData(filter, buffer, end_stream);
    ENVOY_LOG_MISC(debug, "Finished with FilterDataStatus: {}", static_cast<int>(dataStatus));
    if ((end_stream && dataStatus == Http::FilterDataStatus::Continue) || !enabled_) {
      finishFilter(filter);
      return;
    }
  }

  if (data.has_trailers() && enabled_) {
    sendTrailers(filter, data);
    finishFilter(filter);
  }
}

template <>
inline Http::FilterHeadersStatus HttpFilterFuzzer::sendHeaders(Http::StreamDecoderFilter* filter,
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
  Http::FilterHeadersStatus status = filter->decodeHeaders(request_headers_, end_stream);
  if (end_stream) {
    filter->decodeComplete();
  }
  return status;
}

template <>
inline Http::FilterHeadersStatus HttpFilterFuzzer::sendHeaders(Http::StreamEncoderFilter* filter,
                                                               const test::fuzz::HttpData& data,
                                                               bool end_stream) {
  response_headers_ = Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(data.headers());

  // Status must be a valid unsigned long. If not set, the utility function below will throw
  // an exception on the data path of some filters. This should never happen in production, so catch
  // the exception and set to a default value.
  if (!Http::Utility::getResponseStatusOrNullopt(response_headers_).has_value()) {
    response_headers_.setStatus(200);
  }

  ENVOY_LOG_MISC(debug, "Encoding headers (end_stream={}):\n{} ", end_stream, response_headers_);
  Http::FilterHeadersStatus status = filter->encodeHeaders(response_headers_, end_stream);
  if (end_stream) {
    filter->encodeComplete();
  }
  return status;
}

template <> inline void HttpFilterFuzzer::finishFilter(Http::StreamDecoderFilter*) {
  decoding_finished_ = true;
}

template <> inline void HttpFilterFuzzer::finishFilter(Http::StreamEncoderFilter*) {
  encoding_finished_ = true;
}

template <>
inline Http::FilterDataStatus HttpFilterFuzzer::sendData(Http::StreamDecoderFilter* filter,
                                                         Buffer::Instance& buffer,
                                                         bool end_stream) {
  ENVOY_LOG_MISC(debug, "Decoding data (end_stream={}): {} ", end_stream, buffer.toString());
  Http::FilterDataStatus status = filter->decodeData(buffer, end_stream);
  if (end_stream) {
    filter->decodeComplete();
  }
  return status;
}

template <>
inline Http::FilterDataStatus HttpFilterFuzzer::sendData(Http::StreamEncoderFilter* filter,
                                                         Buffer::Instance& buffer,
                                                         bool end_stream) {
  ENVOY_LOG_MISC(debug, "Encoding data (end_stream={}): {} ", end_stream, buffer.toString());
  Http::FilterDataStatus status = filter->encodeData(buffer, end_stream);
  if (end_stream) {
    filter->encodeComplete();
  }
  return status;
}

template <>
inline void HttpFilterFuzzer::sendTrailers(Http::StreamDecoderFilter* filter,
                                           const test::fuzz::HttpData& data) {
  request_trailers_ = Fuzz::fromHeaders<Http::TestRequestTrailerMapImpl>(data.trailers());
  ENVOY_LOG_MISC(debug, "Decoding trailers:\n{} ", request_trailers_);
  filter->decodeTrailers(request_trailers_);
  filter->decodeComplete();
}

template <>
inline void HttpFilterFuzzer::sendTrailers(Http::StreamEncoderFilter* filter,
                                           const test::fuzz::HttpData& data) {
  response_trailers_ = Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(data.trailers());
  ENVOY_LOG_MISC(debug, "Encoding trailers:\n{} ", response_trailers_);
  filter->encodeTrailers(response_trailers_);
  filter->encodeComplete();
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
