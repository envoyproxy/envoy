#include "source/extensions/filters/http/file_server/filter.h"

#include <memory>
#include <utility>

#include "envoy/buffer/buffer.h"

#include "source/common/http/codes.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/file_server/filter_config.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

using ::Envoy::Http::CodeUtility;
using Http::RequestHeaderMap;
using Http::Utility::PercentEncoding;

namespace {
// Returns 0, 0 if range headers are not present or invalid.
std::pair<uint64_t, uint64_t> parseRangeHeader(const Http::RequestHeaderMap& headers) {
  const Envoy::Http::HeaderMap::GetResult range_header =
      headers.get(Envoy::Http::Headers::get().Range);
  if (range_header.size() != 1) {
    return {0, 0};
  }
  absl::string_view range_str = range_header[0]->value().getStringView();
  if (!absl::ConsumePrefix(&range_str, "bytes=")) {
    return {0, 0};
  }
  if (absl::StrContains(range_str, ',')) {
    // Not handling multiple-range requests.
    return {0, 0};
  }
  std::pair<absl::string_view, absl::string_view> split = absl::StrSplit(range_str, '-');
  if (split.first.empty()) {
    // Not handling suffix requests.
    return {0, 0};
  }
  uint64_t start = 0, end = 0;
  if (!absl::SimpleAtoi(split.first, &start)) {
    return {0, 0};
  }
  if (!absl::SimpleAtoi(split.second, &end)) {
    return {start, 0};
  }
  // Add one because range headers use [start, end] and programmers use [start, end)
  return {start, end + 1};
}
} // namespace

const std::string& FileServerFilter::filterName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.http.file_server");
}

void FileServerFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks.addDownstreamWatermarkCallbacks(*this);
  Http::PassThroughDecoderFilter::setDecoderFilterCallbacks(callbacks);
}

void FileServerFilter::onAboveWriteBufferHighWatermark() { file_streamer_.pause(); }

void FileServerFilter::onBelowWriteBufferLowWatermark() { file_streamer_.unpause(); }

Http::FilterHeadersStatus FileServerFilter::decodeHeaders(RequestHeaderMap& headers,
                                                          bool end_stream) {
  if (!decoder_callbacks_->route() || !headers.Path()) {
    return Http::FilterHeadersStatus::Continue;
  }
  const FileServerConfig* config =
      Http::Utility::resolveMostSpecificPerFilterConfig<FileServerConfig>(decoder_callbacks_);
  if (!config) {
    config = file_server_config_.get();
  }
  const std::string path = PercentEncoding::decode(headers.Path()->value().getStringView());
  std::shared_ptr<const ProtoFileServerConfig::PathMapping> mapping = config->pathMapping(path);
  if (!mapping) {
    // If the request didn't match a mapping, skip this filter.
    return Http::FilterHeadersStatus::Continue;
  }
  absl::optional<std::filesystem::path> file_path = config->applyPathMapping(path, *mapping);
  if (!file_path) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       CodeUtility::toString(Http::Code::BadRequest), nullptr,
                                       absl::nullopt, "file_server_rejected_non_normalized_path");
    return Http::FilterHeadersStatus::StopIteration;
  }
  if (!headers.Method()) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       CodeUtility::toString(Http::Code::BadRequest), nullptr,
                                       absl::nullopt, "file_server_rejected_missing_method");
    return Http::FilterHeadersStatus::StopIteration;
  }
  if (headers.Method()->value() != Http::Headers::get().MethodValues.Head &&
      headers.Method()->value() != Http::Headers::get().MethodValues.Get) {
    decoder_callbacks_->sendLocalReply(Http::Code::MethodNotAllowed,
                                       CodeUtility::toString(Http::Code::MethodNotAllowed), nullptr,
                                       absl::nullopt, "file_server_rejected_method");
    return Http::FilterHeadersStatus::StopIteration;
  }
  if (!end_stream) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       CodeUtility::toString(Http::Code::BadRequest), nullptr,
                                       absl::nullopt, "file_server_rejected_not_end_stream");
    return Http::FilterHeadersStatus::StopIteration;
  }
  // Parse range header, if present, into start and end (otherwise or on error, 0,0)
  auto [start, end] = parseRangeHeader(headers);
  is_head_ = headers.Method()->value() == Http::Headers::get().MethodValues.Head;
  file_streamer_.begin(*config, decoder_callbacks_->dispatcher(), start, end,
                       std::move(*file_path));
  return Http::FilterHeadersStatus::StopIteration;
}

bool FileServerFilter::headersFromFile(Http::ResponseHeaderMapPtr response_headers) {
  bool end_response = is_head_ || response_headers->getContentLengthValue() == "0";
  decoder_callbacks_->encodeHeaders(std::move(response_headers), end_response, "file_server");
  headers_sent_ = true;
  return !end_response;
}

void FileServerFilter::bodyChunkFromFile(Buffer::InstancePtr buffer, bool end_stream) {
  decoder_callbacks_->encodeData(*buffer, end_stream);
}

void FileServerFilter::errorFromFile(Http::Code code, absl::string_view log_message) {
  if (!headers_sent_) {
    decoder_callbacks_->sendLocalReply(code, CodeUtility::toString(code), nullptr, absl::nullopt,
                                       log_message);
  } else {
    decoder_callbacks_->streamInfo().setResponseCodeDetails(log_message);
    decoder_callbacks_->resetStream(Http::StreamResetReason::LocalReset, log_message);
  }
}

void FileServerFilter::onDestroy() {
  file_streamer_.abort();
  file_server_config_.reset();
}

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
