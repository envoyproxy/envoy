#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace DynamicForwardProxy {

/**
 * Request-scoped ordered list of upstream hosts for the dynamic forward proxy.
 */
class DynamicHostCandidates : public StreamInfo::FilterState::Object {
public:
  struct Candidate {
    std::string host;
    // Zero selects the cluster default port.
    uint16_t port{};
  };

  static const std::string& key();

  explicit DynamicHostCandidates(std::vector<Candidate> candidates);

  /**
   * Parses a comma separated "host[:port]" list. IPv6 addresses must be bracketed.
   * @return nullptr if the list is empty or an entry is malformed.
   */
  static std::unique_ptr<DynamicHostCandidates> fromString(absl::string_view value);

  const std::vector<Candidate>& candidates() const { return candidates_; }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override;

private:
  const std::vector<Candidate> candidates_;
};

} // namespace DynamicForwardProxy
} // namespace Common
} // namespace Extensions
} // namespace Envoy
