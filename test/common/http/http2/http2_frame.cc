#include "test/common/http/http2/http2_frame.h"

#include <arpa/inet.h>

#include <type_traits>

namespace {

// Make request stream ID in the network byte order
uint32_t makeRequestStreamId(uint32_t stream_id) { return htonl((stream_id << 1) | 1); }

// All this templatized stuff is for the typesafe constexpr bitwise ORing of the "enum class" values
template <typename First, typename... Rest> struct FirstArgType { using type = First; };

template <typename Flag> constexpr uint8_t orFlags(Flag flag) { return static_cast<uint8_t>(flag); }

template <typename Flag, typename... Flags> constexpr uint8_t orFlags(Flag first, Flags... rest) {
  static_assert(std::is_same<Flag, typename FirstArgType<Flags...>::type>::value,
                "All flag types must be the same!");
  return static_cast<uint8_t>(first) | orFlags(rest...);
}

} // namespace

namespace Envoy {
namespace Http {
namespace Http2 {

const char Http2Frame::Preamble[25] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

void Http2Frame::setHeader(absl::string_view header) {
  ASSERT(header.size() >= HeaderSize);
  data_.assign(HeaderSize, 0);
  memcpy(&data_[0], header.data(), HeaderSize);
  data_.resize(HeaderSize + payloadSize());
}

void Http2Frame::setPayload(absl::string_view payload) {
  ASSERT(payload.size() >= payloadSize());
  memcpy(&data_[HeaderSize], payload.data(), payloadSize());
}

uint32_t Http2Frame::payloadSize() const {
  return (uint32_t(data_[0]) << 16) + (uint32_t(data_[1]) << 8) + uint32_t(data_[2]);
}

Http2Frame::ResponseStatus Http2Frame::responseStatus() const {
  if (empty() || Type::HEADERS != type() || size() <= HeaderSize ||
      ((data_[HeaderSize] & 0x80) == 0)) {
    return ResponseStatus::UNKNOWN;
  }
  // See https://tools.ietf.org/html/rfc7541#appendix-A for header values
  switch (static_cast<StaticHeaderIndex>(data_[HeaderSize] & 0x7f)) {
  case StaticHeaderIndex::STATUS_200:
    return ResponseStatus::_200;
  case StaticHeaderIndex::STATUS_404:
    return ResponseStatus::_404;
  default:
    break;
  }
  return ResponseStatus::UNKNOWN;
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

void Http2Frame::appendEmptyHeader() {
  data_.push_back(0x40);
  data_.push_back(0x00);
  data_.push_back(0x00);
}

Http2Frame Http2Frame::makePingFrame(absl::string_view data) {
  static constexpr size_t kPingPayloadSize = 8;
  Http2Frame frame;
  frame.buildHeader(Type::PING, kPingPayloadSize);
  if (!data.empty()) {
    memcpy(&frame.data_[HeaderSize], data.data(), std::min(kPingPayloadSize, data.size()));
  }
  return frame;
}

Http2Frame Http2Frame::makeEmptySettingsFrame(SettingsFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::SETTINGS, 0, static_cast<uint8_t>(flags));
  return frame;
}

Http2Frame Http2Frame::makeEmptyHeadersFrame(uint32_t stream_index, HeadersFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::HEADERS, 0, static_cast<uint8_t>(flags),
                    makeRequestStreamId(stream_index));
  return frame;
}

Http2Frame Http2Frame::makeEmptyContinuationFrame(uint32_t stream_index, HeadersFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::CONTINUATION, 0, static_cast<uint8_t>(flags),
                    makeRequestStreamId(stream_index));
  return frame;
}

Http2Frame Http2Frame::makeEmptyDataFrame(uint32_t stream_index, DataFlags flags) {
  Http2Frame frame;
  frame.buildHeader(Type::DATA, 0, static_cast<uint8_t>(flags), makeRequestStreamId(stream_index));
  return frame;
}

Http2Frame Http2Frame::makePriorityFrame(uint32_t stream_index, uint32_t dependent_index) {
  static constexpr size_t kPriorityPayloadSize = 5;
  Http2Frame frame;
  frame.buildHeader(Type::PRIORITY, kPriorityPayloadSize, 0, makeRequestStreamId(stream_index));
  uint32_t dependent_net = makeRequestStreamId(dependent_index);
  memcpy(&frame.data_[HeaderSize], reinterpret_cast<void*>(&dependent_net), sizeof(uint32_t));
  return frame;
}

Http2Frame Http2Frame::makeWindowUpdateFrame(uint32_t stream_index, uint32_t increment) {
  static constexpr size_t kWindowUpdatePayloadSize = 4;
  Http2Frame frame;
  frame.buildHeader(Type::WINDOW_UPDATE, kWindowUpdatePayloadSize, 0,
                    makeRequestStreamId(stream_index));
  uint32_t increment_net = htonl(increment);
  memcpy(&frame.data_[HeaderSize], reinterpret_cast<void*>(&increment_net), sizeof(uint32_t));
  return frame;
}

Http2Frame Http2Frame::makeMalformedRequest(uint32_t stream_index) {
  Http2Frame frame;
  frame.buildHeader(Type::HEADERS, 0, orFlags(HeadersFlags::END_STREAM, HeadersFlags::END_HEADERS),
                    makeRequestStreamId(stream_index));
  frame.appendStaticHeader(
      StaticHeaderIndex::STATUS_200); // send :status as request header, which is invalid
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makeMalformedRequestWithZerolenHeader(uint32_t stream_index,
                                                             absl::string_view host,
                                                             absl::string_view path) {
  Http2Frame frame;
  frame.buildHeader(Type::HEADERS, 0, orFlags(HeadersFlags::END_STREAM, HeadersFlags::END_HEADERS),
                    makeRequestStreamId(stream_index));
  frame.appendStaticHeader(StaticHeaderIndex::METHOD_GET);
  frame.appendStaticHeader(StaticHeaderIndex::SCHEME_HTTPS);
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::PATH, path);
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::HOST, host);
  frame.appendEmptyHeader();
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makeRequest(uint32_t stream_index, absl::string_view host,
                                   absl::string_view path) {
  Http2Frame frame;
  frame.buildHeader(Type::HEADERS, 0, orFlags(HeadersFlags::END_STREAM, HeadersFlags::END_HEADERS),
                    makeRequestStreamId(stream_index));
  frame.appendStaticHeader(StaticHeaderIndex::METHOD_GET);
  frame.appendStaticHeader(StaticHeaderIndex::SCHEME_HTTPS);
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::PATH, path);
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::HOST, host);
  frame.adjustPayloadSize();
  return frame;
}

Http2Frame Http2Frame::makePostRequest(uint32_t stream_index, absl::string_view host,
                                       absl::string_view path) {
  Http2Frame frame;
  frame.buildHeader(Type::HEADERS, 0, orFlags(HeadersFlags::END_HEADERS),
                    makeRequestStreamId(stream_index));
  frame.appendStaticHeader(StaticHeaderIndex::METHOD_POST);
  frame.appendStaticHeader(StaticHeaderIndex::SCHEME_HTTPS);
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::PATH, path);
  frame.appendHeaderWithoutIndexing(StaticHeaderIndex::HOST, host);
  frame.adjustPayloadSize();
  return frame;
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
