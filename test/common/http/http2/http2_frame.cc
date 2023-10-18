#include "test/common/http/http2/http2_frame.h"

#include <type_traits>

#include "envoy/common/platform.h"

#include "source/common/common/hex.h"

#include "quiche/spdy/core/hpack/hpack_encoder.h"

namespace {

// Converts stream ID to the network byte order. Supports all values in the range [0, 2^30).
uint32_t makeNetworkOrderStreamId(uint32_t stream_id) { return htonl(stream_id); }

} // namespace

namespace Envoy {
namespace Http {
namespace Http2 {

const char Http2Frame::Preamble[25] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

void Http2Frame::setHeader(absl::string_view header) {
  ASSERT(header.size() >= HeaderSize);
  data_.assign(HeaderSize, 0);
  // TODO(adisuissa): memcpy is discouraged as it may be unsafe. This should be
  // use a safer memcpy alternative (example: https://abseil.io/tips/93)
  memcpy(data_.data(), header.data(), HeaderSize);
  data_.resize(HeaderSize + payloadSize());
}

void Http2Frame::setPayload(absl::string_view payload) {
  ASSERT(payload.size() >= payloadSize());
  ASSERT(data_.capacity() >= HeaderSize + payloadSize());
  memcpy(&data_[HeaderSize], payload.data(), payloadSize());
}

uint32_t Http2Frame::payloadSize() const {
  return (uint32_t(data_[0]) << 16) + (uint32_t(data_[1]) << 8) + uint32_t(data_[2]);
}

Http2Frame::ResponseStatus Http2Frame::responseStatus() const {
  if (empty() || Type::Headers != type() || size() <= HeaderSize ||
      ((data_[HeaderSize] & 0x80) == 0)) {
    return ResponseStatus::Unknown;
  }
  // See https://tools.ietf.org/html/rfc7541#appendix-A for header values
  switch (static_cast<StaticHeaderIndex>(data_[HeaderSize] & 0x7f)) {
  case StaticHeaderIndex::Status200:
    return ResponseStatus::Ok;
  case StaticHeaderIndex::Status404:
    return ResponseStatus::NotFound;
  case StaticHeaderIndex::Status500:
    return ResponseStatus::InternalServerError;
  default:
    break;
  }
  return ResponseStatus::Unknown;
}

uint32_t Http2Frame::streamId() const {
  if (empty() || size() <= HeaderSize) {
    return 0;
  }
  return (uint32_t(data_[5]) << 24) + (uint32_t(data_[6]) << 16) + (uint32_t(data_[7]) << 8) +
         uint32_t(data_[8]);
}

void Http2Frame::buildHeader(Type type, uint32_t payload_size, uint8_t flags, uint32_t stream_id) {
  data_.assign(payload_size + HeaderSize, 0);
  setPayloadSize(payload_size);
  data_[3] = static_cast<uint8_t>(type);
  data_[4] = flags;
  if (stream_id) {
    memcpy(&data_[5], &stream_id, sizeof(stream_id));
  }
}

void Http2Frame::setPayloadSize(uint32_t size) {
  data_[0] = (size >> 16) & 0xff;
  data_[1] = (size >> 8) & 0xff;
  data_[2] = size & 0xff;
}

void Http2Frame::appendHpackInt(uint64_t value, unsigned char prefix_mask) {
  if (value < prefix_mask) {
    data_.push_back(value);
  } else {
    data_.push_back(prefix_mask);
    value -= prefix_mask;

    while (value >= 128) {
      data_.push_back((value & 0x7f) | 0x80);
      value >>= 7;
    }
    data_.push_back(value);
  }
}

// See https://tools.ietf.org/html/rfc7541#section-6.1 for header representations

void Http2Frame::appendStaticHeader(StaticHeaderIndex index) {
  data_.push_back(0x80 | static_cast<uint8_t>(index));
}

void Http2Frame::appendHeaderWithoutIndexing(StaticHeaderIndex index, absl::string_view value) {
  appendHpackInt(static_cast<uint8_t>(index), 0xf);
  appendHpackInt(value.size(), 0x7f);
  appendData(value);
}

void Http2Frame::appendHeaderWithoutIndexing(const Header& header) {
  data_.push_back(0);
  appendHpackInt(header.key_.size(), 0x7f);
  appendData(header.key_);
  appendHpackInt(header.value_.size(), 0x7f);
  appendData(header.value_);
}

void Http2Frame::appendEmptyHeader() {
  data_.push_back(0x40);
  data_.push_back(0x00);
  data_.push_back(0x00);
}

Http2Frame Http2Frame::makeRawFrame(Type type, uint8_t flags, uint32_t stream_id,
                                    absl::string_view payload) {
  Http2Frame frame;
  frame.buildHeader(type, 0, flags, makeNetworkOrderStreamId(stream_id));
  frame.appendData(payload);
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makePingFrame(absl::string_view data) {
  static constexpr size_t kPingPayloadSize = 8;
  Http2Frame frame;
  frame.buildHeader(Type::Ping, kPingPayloadSize);
  ASSERT(frame.data_.capacity() >= HeaderSize + std::min(kPingPayloadSize, data.size()));
  if (!data.empty()) {
    memcpy(&frame.data_[HeaderSize], data.data(), std::min(kPingPayloadSize, data.size()));
  }
  return frame;
}

Http2Frame Http2Frame::makeEmptySettingsFrame(SettingsFlags flags) {
  return makeSettingsFrame(flags, {});
}

Http2Frame Http2Frame::makeSettingsFrame(SettingsFlags flags,
                                         std::list<std::pair<uint16_t, uint32_t>> settings) {
  Http2Frame frame;
  frame.buildHeader(Type::Settings, 0, static_cast<uint8_t>(flags));
  for (auto& item : settings) {
    frame.data_.push_back((item.first >> 8) & 0xff);
    frame.data_.push_back(item.first & 0xff);
    frame.data_.push_back((item.second >> 24) & 0xff);
    frame.data_.push_back((item.second >> 16) & 0xff);
    frame.data_.push_back((item.second >> 8) & 0xff);
    frame.data_.push_back(item.second & 0xff);
  }
  frame.setPayloadSize(6 * settings.size());
  return frame;
}

Http2Frame Http2Frame::makeEmptyHeadersFrame(uint32_t stream_index, HeadersFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::Headers, 0, static_cast<uint8_t>(flags),
                    makeNetworkOrderStreamId(stream_index));
  return frame;
}

Http2Frame Http2Frame::makeHeadersFrameNoStatus(uint32_t stream_index) {
  Http2Frame frame;
  frame.buildHeader(
      Type::Headers, 0,
      static_cast<uint8_t>(orFlags(HeadersFlags::EndStream, HeadersFlags::EndHeaders)),
      makeNetworkOrderStreamId(stream_index));
  return frame;
}

Http2Frame Http2Frame::makeHeadersFrameWithStatus(std::string status, uint32_t stream_index,
                                                  HeadersFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::Headers, 0, static_cast<uint8_t>(flags),
                    makeNetworkOrderStreamId(stream_index));
  if (status == "200") {
    frame.appendStaticHeader(StaticHeaderIndex::Status200);
  } else if (status == "204") {
    frame.appendStaticHeader(StaticHeaderIndex::Status204);
  } else if (status == "206") {
    frame.appendStaticHeader(StaticHeaderIndex::Status206);
  } else if (status == "304") {
    frame.appendStaticHeader(StaticHeaderIndex::Status304);
  } else if (status == "400") {
    frame.appendStaticHeader(StaticHeaderIndex::Status400);
  } else if (status == "500") {
    frame.appendStaticHeader(StaticHeaderIndex::Status500);
  } else { // Not a static header
    Header statusHeader = Header(":status", status);
    frame.appendHeaderWithoutIndexing(statusHeader);
  }
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makeEmptyContinuationFrame(uint32_t stream_index, HeadersFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::Continuation, 0, static_cast<uint8_t>(flags),
                    makeNetworkOrderStreamId(stream_index));
  return frame;
}

Http2Frame Http2Frame::makeEmptyDataFrame(uint32_t stream_index, DataFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::Data, 0, static_cast<uint8_t>(flags),
                    makeNetworkOrderStreamId(stream_index));
  return frame;
}

Http2Frame Http2Frame::makePriorityFrame(uint32_t stream_index, uint32_t dependent_index) {
  static constexpr size_t kPriorityPayloadSize = 5;
  Http2Frame frame;
  frame.buildHeader(Type::Priority, kPriorityPayloadSize, 0,
                    makeNetworkOrderStreamId(stream_index));
  const uint32_t dependent_net = makeNetworkOrderStreamId(dependent_index);
  ASSERT(frame.data_.capacity() >= HeaderSize + sizeof(uint32_t));
  memcpy(&frame.data_[HeaderSize], reinterpret_cast<const void*>(&dependent_net), sizeof(uint32_t));
  return frame;
}

Http2Frame Http2Frame::makeEmptyPushPromiseFrame(uint32_t stream_index,
                                                 uint32_t promised_stream_index,
                                                 HeadersFlags flags) {
  static constexpr size_t kEmptyPushPromisePayloadSize = 4;
  Http2Frame frame;
  frame.buildHeader(Type::PushPromise, kEmptyPushPromisePayloadSize, static_cast<uint8_t>(flags),
                    makeNetworkOrderStreamId(stream_index));
  const uint32_t promised_stream_id = makeNetworkOrderStreamId(promised_stream_index);
  ASSERT(frame.data_.capacity() >= HeaderSize + sizeof(uint32_t));
  memcpy(&frame.data_[HeaderSize], reinterpret_cast<const void*>(&promised_stream_id),
         sizeof(uint32_t));
  return frame;
}

Http2Frame Http2Frame::makeResetStreamFrame(uint32_t stream_index, ErrorCode error_code) {
  static constexpr size_t kResetStreamPayloadSize = 4;
  Http2Frame frame;
  frame.buildHeader(Type::RstStream, kResetStreamPayloadSize, 0,
                    makeNetworkOrderStreamId(stream_index));
  const uint32_t error = static_cast<uint32_t>(error_code);
  ASSERT(frame.data_.capacity() >= HeaderSize + sizeof(uint32_t));
  memcpy(&frame.data_[HeaderSize], reinterpret_cast<const void*>(&error), sizeof(uint32_t));
  return frame;
}

Http2Frame Http2Frame::makeEmptyGoAwayFrame(uint32_t last_stream_index, ErrorCode error_code) {
  static constexpr size_t kEmptyGoAwayPayloadSize = 8;
  Http2Frame frame;
  frame.buildHeader(Type::GoAway, kEmptyGoAwayPayloadSize, 0);
  const uint32_t last_stream_id = makeNetworkOrderStreamId(last_stream_index);
  ASSERT(frame.data_.capacity() >= HeaderSize + 4 + sizeof(uint32_t));
  memcpy(&frame.data_[HeaderSize], reinterpret_cast<const void*>(&last_stream_id),
         sizeof(uint32_t));
  const uint32_t error = static_cast<uint32_t>(error_code);
  memcpy(&frame.data_[HeaderSize + 4], reinterpret_cast<const void*>(&error), sizeof(uint32_t));
  return frame;
}

Http2Frame Http2Frame::makeWindowUpdateFrame(uint32_t stream_index, uint32_t increment) {
  static constexpr size_t kWindowUpdatePayloadSize = 4;
  Http2Frame frame;
  frame.buildHeader(Type::WindowUpdate, kWindowUpdatePayloadSize, 0,
                    makeNetworkOrderStreamId(stream_index));
  const uint32_t increment_net = htonl(increment);
  ASSERT(frame.data_.capacity() >= HeaderSize + sizeof(uint32_t));
  memcpy(&frame.data_[HeaderSize], reinterpret_cast<const void*>(&increment_net), sizeof(uint32_t));
  return frame;
}

// Note: encoder in codebase persists multiple maps, with each map representing an individual frame.
Http2Frame Http2Frame::makeMetadataFrameFromMetadataMap(uint32_t stream_index,
                                                        const MetadataMap& metadata_map,
                                                        MetadataFlags flags) {
  const int numberOfNameValuePairs = metadata_map.size();
  spdy::HpackEncoder encoder;
  encoder.DisableCompression();
  spdy::HpackEncoder::Representations representations;
  representations.reserve(numberOfNameValuePairs);
  for (const auto& metadata : metadata_map) {
    representations.push_back({metadata.first, metadata.second});
  }

  auto payload_sequence = encoder.EncodeRepresentations(representations);
  ASSERT(payload_sequence->HasNext() || numberOfNameValuePairs == 0);
  // Concatenate all the payload segments to a single string.
  std::string payload;
  const size_t maxPayloadSegmentSize = 4 * 1024 * 1024;
  while (payload_sequence->HasNext()) {
    absl::StrAppend(&payload, payload_sequence->Next(maxPayloadSegmentSize));
  }

  Http2Frame frame;
  frame.buildHeader(Type::Metadata, payload.size(), static_cast<uint8_t>(flags),
                    makeNetworkOrderStreamId(stream_index));
  frame.appendDataAfterHeaders(payload);
  ASSERT(frame.size() == payload.size() + 9);
  return frame;
}

Http2Frame Http2Frame::makeMalformedRequest(uint32_t stream_index) {
  Http2Frame frame;
  frame.buildHeader(Type::Headers, 0, orFlags(HeadersFlags::EndStream, HeadersFlags::EndHeaders),
                    makeNetworkOrderStreamId(stream_index));
  frame.appendStaticHeader(
      StaticHeaderIndex::Status200); // send :status as request header, which is invalid
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makeMalformedRequestWithZerolenHeader(uint32_t stream_index,
                                                             absl::string_view host,
                                                             absl::string_view path) {
  Http2Frame frame;
  frame.buildHeader(Type::Headers, 0, orFlags(HeadersFlags::EndStream, HeadersFlags::EndHeaders),
                    makeNetworkOrderStreamId(stream_index));
  frame.appendStaticHeader(StaticHeaderIndex::MethodGet);
  frame.appendStaticHeader(StaticHeaderIndex::SchemeHttps);
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::Path, path);
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::Authority, host);
  frame.appendEmptyHeader();
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makeMalformedResponseWithZerolenHeader(uint32_t stream_index) {
  Http2Frame frame;
  frame.buildHeader(Type::Headers, 0, orFlags(HeadersFlags::EndStream, HeadersFlags::EndHeaders),
                    makeNetworkOrderStreamId(stream_index));
  frame.appendStaticHeader(StaticHeaderIndex::Status200);
  frame.appendEmptyHeader();
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makeRequest(uint32_t stream_index, absl::string_view host,
                                   absl::string_view path) {
  Http2Frame frame;
  frame.buildHeader(Type::Headers, 0, orFlags(HeadersFlags::EndStream, HeadersFlags::EndHeaders),
                    makeNetworkOrderStreamId(stream_index));
  frame.appendStaticHeader(StaticHeaderIndex::MethodGet);
  frame.appendStaticHeader(StaticHeaderIndex::SchemeHttps);
  if (path.empty() || path == "/") {
    frame.appendStaticHeader(StaticHeaderIndex::Path);
  } else {
    frame.appendHeaderWithoutIndexing(StaticHeaderIndex::Path, path);
  }
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::Authority, host);
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makeRequest(uint32_t stream_index, absl::string_view host,
                                   absl::string_view path,
                                   const std::vector<Header> extra_headers) {
  auto frame = makeRequest(stream_index, host, path);
  for (const auto& header : extra_headers) {
    frame.appendHeaderWithoutIndexing(header);
  }
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makePostRequest(uint32_t stream_index, absl::string_view host,
                                       absl::string_view path) {
  Http2Frame frame;
  frame.buildHeader(Type::Headers, 0, orFlags(HeadersFlags::EndHeaders),
                    makeNetworkOrderStreamId(stream_index));
  frame.appendStaticHeader(StaticHeaderIndex::MethodPost);
  frame.appendStaticHeader(StaticHeaderIndex::SchemeHttps);
  if (path.empty() || path == "/") {
    frame.appendStaticHeader(StaticHeaderIndex::Path);
  } else {
    frame.appendHeaderWithoutIndexing(StaticHeaderIndex::Path, path);
  }
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::Authority, host);
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makePostRequest(uint32_t stream_index, absl::string_view host,
                                       absl::string_view path,
                                       const std::vector<Header> extra_headers) {

  auto frame = makePostRequest(stream_index, host, path);
  for (const auto& header : extra_headers) {
    frame.appendHeaderWithoutIndexing(header);
  }
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makeGenericFrame(absl::string_view contents) {
  Http2Frame frame;
  frame.appendData(contents);
  return frame;
}

Http2Frame Http2Frame::makeGenericFrameFromHexDump(absl::string_view contents) {
  Http2Frame frame;
  frame.appendData(Hex::decode(std::string(contents)));
  return frame;
}

Http2Frame Http2Frame::makeDataFrame(uint32_t stream_index, absl::string_view data,
                                     DataFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::Data, 0, static_cast<uint8_t>(flags),
                    makeNetworkOrderStreamId(stream_index));
  frame.appendData(data);
  frame.adjustPayloadSize();
  return frame;
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
