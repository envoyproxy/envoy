#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/http/metadata_interface.h"

#include "source/common/common/assert.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace Http2 {

template <typename Flag> constexpr uint8_t orFlags(Flag flag) { return static_cast<uint8_t>(flag); }

// All this templatized stuff is for the typesafe constexpr bitwise ORing of the "enum class" values
template <typename First, typename... Rest> struct FirstArgType {
  using type = First; // NOLINT(readability-identifier-naming)
};

template <typename Flag, typename... Flags> constexpr uint8_t orFlags(Flag first, Flags... rest) {
  static_assert(std::is_same<Flag, typename FirstArgType<Flags...>::type>::value,
                "All flag types must be the same!");
  return static_cast<uint8_t>(first) | orFlags(rest...);
}

// Rudimentary facility for building and parsing of HTTP2 frames for unit tests
class Http2Frame {
  using DataContainer = std::vector<uint8_t>;

public:
  Http2Frame() = default;

  using Iterator = DataContainer::iterator;
  using ConstIterator = DataContainer::const_iterator;

  static constexpr size_t HeaderSize = 9;
  static const char Preamble[25];

  enum class Type : uint8_t {
    Data = 0,
    Headers,
    Priority,
    RstStream,
    Settings,
    PushPromise,
    Ping,
    GoAway,
    WindowUpdate,
    Continuation,
    Metadata = 77,
  };

  enum class SettingsFlags : uint8_t {
    None = 0,
    Ack = 1,
  };

  enum class HeadersFlags : uint8_t {
    None = 0,
    EndStream = 1,
    EndHeaders = 4,
  };

  enum class DataFlags : uint8_t {
    None = 0,
    EndStream = 1,
  };

  enum class MetadataFlags : uint8_t {
    None = 0,
    EndMetadata = 4,
  };

  // See https://tools.ietf.org/html/rfc7541#appendix-A for static header indexes
  enum class StaticHeaderIndex : uint8_t {
    Unknown,
    MethodGet = 2,
    MethodPost = 3,
    Path = 4,
    Status200 = 8,
    Status204 = 9,
    Status206 = 10,
    Status304 = 11,
    Status400 = 12,
    Status404 = 13,
    Status500 = 14,
    SchemeHttps = 7,
    Host = 38,
  };

  enum class ErrorCode : uint8_t {
    NoError = 0,
    ProtocolError,
    InternalError,
    FlowControlError,
    SettingsTimeout,
    StreamClosed,
    FrameSizeError,
    RefusedStream,
    Cancel,
    CompressionError,
    ConnectError,
    EnhanceYourCalm,
    InadequateSecurity,
    Http11Required
  };

  enum class ResponseStatus { Unknown, Ok, NotFound };

  struct Header {
    Header(absl::string_view key, absl::string_view value) : key_(key), value_(value) {}
    std::string key_;
    std::string value_;
  };

  /**
   * Make client stream ID out of the given ID in the host byte order, ensuring that the stream id
   * is odd as required by https://tools.ietf.org/html/rfc7540#section-5.1.1
   * Use this function to create client stream ids for methods creating HTTP/2 frames.
   * @param stream_id some stream id that will be used to create the client stream id.
   * @return an odd number client stream id.
   */
  static uint32_t makeClientStreamId(uint32_t stream_id) { return (stream_id << 1) | 1; }

  // Methods for creating HTTP2 frames
  static Http2Frame makePingFrame(absl::string_view data = {});
  static Http2Frame makeEmptySettingsFrame(SettingsFlags flags = SettingsFlags::None);
  static Http2Frame makeEmptyHeadersFrame(uint32_t stream_index,
                                          HeadersFlags flags = HeadersFlags::None);
  static Http2Frame makeHeadersFrameNoStatus(uint32_t stream_index);
  static Http2Frame makeHeadersFrameWithStatus(
      std::string status, uint32_t stream_index,
      HeadersFlags flags = static_cast<HeadersFlags>(orFlags(HeadersFlags::EndStream,
                                                             HeadersFlags::EndHeaders)));
  // TODO: MakeHeadersFrameWithStatusAndNonStaticHeaders
  static Http2Frame makeEmptyContinuationFrame(uint32_t stream_index,
                                               HeadersFlags flags = HeadersFlags::None);
  static Http2Frame makeEmptyDataFrame(uint32_t stream_index, DataFlags flags = DataFlags::None);
  static Http2Frame makePriorityFrame(uint32_t stream_index, uint32_t dependent_index);

  static Http2Frame makeEmptyPushPromiseFrame(uint32_t stream_index, uint32_t promised_stream_index,
                                              HeadersFlags flags = HeadersFlags::None);
  static Http2Frame makeResetStreamFrame(uint32_t stream_index, ErrorCode error_code);
  static Http2Frame makeEmptyGoAwayFrame(uint32_t last_stream_index, ErrorCode error_code);

  static Http2Frame makeWindowUpdateFrame(uint32_t stream_index, uint32_t increment);
  static Http2Frame makeMetadataFrameFromMetadataMap(uint32_t stream_index,
                                                     const MetadataMap& metadata_map,
                                                     MetadataFlags flags);

  static Http2Frame makeMalformedRequest(uint32_t stream_index);
  static Http2Frame makeMalformedRequestWithZerolenHeader(uint32_t stream_index,
                                                          absl::string_view host,
                                                          absl::string_view path);
  static Http2Frame makeMalformedResponseWithZerolenHeader(uint32_t stream_index);
  static Http2Frame makeRequest(uint32_t stream_index, absl::string_view host,
                                absl::string_view path);
  static Http2Frame makeRequest(uint32_t stream_index, absl::string_view host,
                                absl::string_view path, const std::vector<Header> extra_headers);
  static Http2Frame makePostRequest(uint32_t stream_index, absl::string_view host,
                                    absl::string_view path);
  static Http2Frame makePostRequest(uint32_t stream_index, absl::string_view host,
                                    absl::string_view path,
                                    const std::vector<Header> extra_headers);
  static Http2Frame makeDataFrame(uint32_t stream_index, absl::string_view data,
                                  DataFlags flags = DataFlags::None);

  /**
   * Creates a frame with the given contents. This frame can be
   * malformed/invalid depending on the given contents.
   * @param contents the contents of the newly created frame.
   * @return an Http2Frame that is comprised of the given contents.
   */
  static Http2Frame makeGenericFrame(absl::string_view contents);
  static Http2Frame makeGenericFrameFromHexDump(absl::string_view contents);

  Type type() const { return static_cast<Type>(data_[3]); }
  ResponseStatus responseStatus() const;

  // Copy HTTP2 header. The `header` parameter must at least be HeaderSize long.
  // Allocates payload size based on the value in the header.
  void setHeader(absl::string_view header);

  // Copy payloadSize() bytes from the `payload`. The `payload` must be at least payloadSize() long.
  void setPayload(absl::string_view payload);

  // Convert to `std::string` for convenience.
  explicit operator std::string() const {
    if (data_.empty()) {
      return {};
    }
    return std::string(reinterpret_cast<const char*>(data()), size());
  }

  uint32_t payloadSize() const;
  // Total size of the frame
  size_t size() const { return data_.size(); }
  // Access to the raw frame bytes
  const uint8_t* data() const { return data_.data(); }
  Iterator begin() { return data_.begin(); }
  Iterator end() { return data_.end(); }
  ConstIterator begin() const { return data_.begin(); }
  ConstIterator end() const { return data_.end(); }
  bool empty() const { return data_.empty(); }

private:
  void buildHeader(Type type, uint32_t payload_size = 0, uint8_t flags = 0, uint32_t stream_id = 0);
  void setPayloadSize(uint32_t size);

  // This method appends HPACK encoded uint64_t to the payload. adjustPayloadSize() must be called
  // after calling this method (possibly multiple times) to write new payload length to the HTTP2
  // header.
  void appendHpackInt(uint64_t value, unsigned char prefix_mask);
  void appendData(absl::string_view data) { data_.insert(data_.end(), data.begin(), data.end()); }
  void appendData(std::vector<uint8_t> data) {
    data_.insert(data_.end(), data.begin(), data.end());
  }
  void appendDataAfterHeaders(std::vector<uint8_t> data) {
    std::copy(data.begin(), data.end(), data_.begin() + 9);
  }

  // Headers are directly encoded
  void appendStaticHeader(StaticHeaderIndex index);
  void appendHeaderWithoutIndexing(StaticHeaderIndex index, absl::string_view value);
  void appendHeaderWithoutIndexing(const Header& header);
  void appendEmptyHeader();

  // This method updates payload length in the HTTP2 header based on the size of the data_
  void adjustPayloadSize() {
    ASSERT(size() >= HeaderSize);
    setPayloadSize(size() - HeaderSize);
  }

  DataContainer data_;
};

} // namespace Http2
} // namespace Http
} // namespace Envoy
