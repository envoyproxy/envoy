#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/common/assert.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace Http2 {

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
    Continuation
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

  // See https://tools.ietf.org/html/rfc7541#appendix-A for static header indexes
  enum class StaticHeaderIndex : uint8_t {
    Unknown,
    MethodGet = 2,
    MethodPost = 3,
    Path = 4,
    Status200 = 8,
    Status404 = 13,
    SchemeHttps = 7,
    Host = 38,
  };

  enum class ResponseStatus { Unknown, Ok, NotFound };

  // Methods for creating HTTP2 frames
  static Http2Frame makePingFrame(absl::string_view data = {});
  static Http2Frame makeEmptySettingsFrame(SettingsFlags flags = SettingsFlags::None);
  static Http2Frame makeEmptyHeadersFrame(uint32_t stream_index,
                                          HeadersFlags flags = HeadersFlags::None);
  static Http2Frame makeEmptyContinuationFrame(uint32_t stream_index,
                                               HeadersFlags flags = HeadersFlags::None);
  static Http2Frame makeEmptyDataFrame(uint32_t stream_index, DataFlags flags = DataFlags::None);
  static Http2Frame makePriorityFrame(uint32_t stream_index, uint32_t dependent_index);
  static Http2Frame makeWindowUpdateFrame(uint32_t stream_index, uint32_t increment);
  static Http2Frame makeMalformedRequest(uint32_t stream_index);
  static Http2Frame makeMalformedRequestWithZerolenHeader(uint32_t stream_index,
                                                          absl::string_view host,
                                                          absl::string_view path);
  static Http2Frame makeRequest(uint32_t stream_index, absl::string_view host,
                                absl::string_view path);
  static Http2Frame makePostRequest(uint32_t stream_index, absl::string_view host,
                                    absl::string_view path);

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

  // Headers are directly encoded
  void appendStaticHeader(StaticHeaderIndex index);
  void appendHeaderWithoutIndexing(StaticHeaderIndex index, absl::string_view value);
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
