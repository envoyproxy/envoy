#pragma once

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// Kafka header.
using Header = std::pair<absl::string_view, absl::string_view>;

// Binds a single inbound record from Kafka client with its delivery information.
struct OutboundRecord {

  // These fields were received from downstream.
  const std::string topic_;
  const int32_t partition_;
  const absl::string_view key_;
  const absl::string_view value_;
  const std::vector<Header> headers_;

  // These fields will get updated when delivery to upstream Kafka cluster finishes.
  int16_t error_code_{0};
  uint32_t saved_offset_{0};

  OutboundRecord(const std::string& topic, const int32_t partition, const absl::string_view key,
                 const absl::string_view value, const std::vector<Header>& headers)
      : topic_{topic}, partition_{partition}, key_{key}, value_{value}, headers_{headers} {};
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
