#pragma once

#include <string>

#include "common/http/header_map_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class MessageMetadata {
public:
  MessageMetadata() = default;

  void setOneWay(bool oneway) { is_oneway_ = oneway; }
  bool isOneWay() const { return is_oneway_; }

  bool hasTopicName() const { return topic_name_.has_value(); }
  const std::string& topicName() const { return topic_name_.value(); }
  void setTopicName(const std::string& topic_name) { topic_name_ = topic_name; }

  /**
   * @return HeaderMap of current headers
   */
  const Http::HeaderMap& headers() const { return *headers_; }
  Http::HeaderMap& headers() { return *headers_; }

private:
  bool is_oneway_{false};
  absl::optional<std::string> topic_name_{};

  Http::HeaderMapPtr headers_{Http::RequestHeaderMapImpl::create()};
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy