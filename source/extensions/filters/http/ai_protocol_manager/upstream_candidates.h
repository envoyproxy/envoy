#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/stream_info/filter_state.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// One place the request may go. An empty rewrite keeps the client's value.
struct UpstreamCandidate {
  // [A-Za-z0-9_.-]{1,32} and unique in its list; logged and used as the priority group name.
  std::string name;
  std::string cluster;
  std::string model;
  std::string path;
};

// The stream's upstream decision: seeded by an AI filter, then sealed, committed and published by
// the AI Protocol Manager. Read-only to everyone else, since only friends reach its mutators.
class UpstreamCandidates : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view kFilterStateKey = "envoy.ai.upstream.candidates";

  enum class Outcome { Pending, Committed, Rejected, NotFollowed };

  struct Entry {
    UpstreamCandidate candidate;
    // "<AI filter entry name>[/<detail>]".
    std::string added_by;
    // Both empty while the entry is eligible.
    std::string excluded_by;
    std::string excluded_reason;
  };

  // nullptr when the key is unset or holds another type.
  static const UpstreamCandidates* fromFilterState(const StreamInfo::FilterState& state);

  const std::vector<Entry>& entries() const { return entries_; }
  Outcome outcome() const { return outcome_; }
  // Set only when Committed.
  const Entry* chosen() const { return chosen_.has_value() ? &entries_[*chosen_] : nullptr; }

  // StreamInfo::FilterState::Object
  std::optional<std::string> serializeAsString() const override;
  bool hasFieldSupport() const override { return true; }
  FieldType getField(absl::string_view field) const override;

private:
  friend class StageCallbacks;
  friend class UpstreamCandidatesCommit;
  friend class UpstreamCandidatesPeer;

  UpstreamCandidates() = default;

  // InvalidArgument on an empty list, an empty cluster, a bad path, or a bad or duplicate name.
  static absl::StatusOr<std::shared_ptr<UpstreamCandidates>>
  create(std::vector<UpstreamCandidate> candidates, std::string added_by);

  void exclude(size_t index, absl::string_view by, absl::string_view reason);
  // `chosen` is set exactly when `outcome` is Committed.
  void setOutcome(Outcome outcome, std::optional<size_t> chosen = std::nullopt);

  std::vector<Entry> entries_;
  Outcome outcome_{Outcome::Pending};
  std::optional<size_t> chosen_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
