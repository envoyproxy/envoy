#pragma once

#include <string.h>

#include <algorithm>
#include <list>
#include <string>

#include "common/common/macros.h"

#include "extensions/filters/network/thrift_proxy/thrift.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Header is a name-value pair in Thrift transport or protocol headers.
 */
class Header {
public:
  Header(const std::string key, const std::string value) : key_(key), value_(value) {}
  Header(const Header& rhs) : key_(rhs.key_), value_(rhs.value_) {}

  const std::string& key() const { return key_; }
  const std::string& value() const { return value_; }

private:
  std::string key_;
  std::string value_;
};

// TODO(zuercher): replace this with Http::HeaderMap[Impl]
/*
 * HeaderMap contains Thrift transport and/or protocol-level headers.
 */
class HeaderMap {
public:
  HeaderMap() {}
  HeaderMap(const std::initializer_list<std::pair<std::string, std::string>>& values);
  HeaderMap(const HeaderMap& rhs);

  /**
   * @return true if the HeaderMap is empty
   */
  bool empty() const { return headers_.empty(); }

  /**
   * @return uint32_t the number of headers in the map
   */
  uint32_t size() const { return headers_.size(); }

  /**
   * @param header Header to move into the HeaderMap
   */
  void add(Header&& header) { headers_.emplace_back(std::move(header)); }

  /**
   * Clears all Headers from the HeaderMap.
   */
  void clear() { headers_.clear(); }

  /**
   * Retrieves a Header from the HeaderMap.
   * @param key std::string containing the key to lookup
   * @return Header* corresponding to key or nullptr if not found.
   */
  Header* get(const std::string& key);

  /**
   * Const iterators for the HeaderMap.
   */
  std::list<Header>::const_iterator begin() const noexcept { return headers_.begin(); }
  std::list<Header>::const_iterator end() const noexcept { return headers_.end(); }
  std::list<Header>::const_iterator cbegin() const noexcept { return headers_.cbegin(); }
  std::list<Header>::const_iterator cend() const noexcept { return headers_.cend(); }

  /**
   * For testing. Equality is based on equality of the backing list. This is an exact match
   * comparison (order matters).
   */
  bool operator==(const HeaderMap& rhs) const;

  /**
   * @return an empty HeaderMap
   */
  static const HeaderMap& emptyHeaderMap() { CONSTRUCT_ON_FIRST_USE(HeaderMap, HeaderMap({})); }

private:
  std::list<Header> headers_;
};

/**
 * MessageMetadata encapsulates metadata about Thrift messages. The various fields are considered
 * optional since they may come from either the transport or protocol in some cases. Unless
 * otherwise noted, accessor methods throw absl::bad_optional_access if the corresponding value has
 * not been set.
 */
class MessageMetadata {
public:
  MessageMetadata() {}

  bool hasFrameSize() const { return frame_size_.has_value(); }
  uint32_t frameSize() const { return frame_size_.value(); }
  void setFrameSize(uint32_t size) { frame_size_ = size; }

  bool hasProtocol() const { return proto_.has_value(); }
  ProtocolType protocol() const { return proto_.value(); }
  void setProtocol(ProtocolType proto) { proto_ = proto; }

  bool hasMethodName() const { return method_name_.has_value(); }
  const std::string& methodName() const { return method_name_.value(); }
  void setMethodName(const std::string& method_name) { method_name_ = method_name; }

  bool hasSequenceId() const { return seq_id_.has_value(); }
  int32_t sequenceId() const { return seq_id_.value(); }
  void setSequenceId(int32_t seq_id) { seq_id_ = seq_id; }

  bool hasMessageType() const { return msg_type_.has_value(); }
  MessageType messageType() const { return msg_type_.value(); }
  void setMessageType(MessageType msg_type) { msg_type_ = msg_type; }

  void addHeader(Header&& header) { headers_.add(std::move(header)); }
  /**
   * @return HeaderMap of current headers (never throws)
   */
  const HeaderMap& headers() const { return headers_; }

  bool hasAppException() const { return app_ex_type_.has_value(); }
  void setAppException(AppExceptionType app_ex_type, const std::string& message) {
    app_ex_type_ = app_ex_type;
    app_ex_msg_ = message;
  }
  AppExceptionType appExceptionType() const { return app_ex_type_.value(); }
  const std::string& appExceptionMessage() const { return app_ex_msg_.value(); }

private:
  absl::optional<uint32_t> frame_size_{};
  absl::optional<ProtocolType> proto_{};
  absl::optional<std::string> method_name_{};
  absl::optional<int32_t> seq_id_{};
  absl::optional<MessageType> msg_type_{};
  HeaderMap headers_;
  absl::optional<AppExceptionType> app_ex_type_;
  absl::optional<std::string> app_ex_msg_;
};

typedef std::shared_ptr<MessageMetadata> MessageMetadataSharedPtr;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
