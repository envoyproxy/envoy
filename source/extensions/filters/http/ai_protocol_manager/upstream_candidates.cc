#include "source/extensions/filters/http/ai_protocol_manager/upstream_candidates.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/http/header_map.h"

#include "source/common/common/assert.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

constexpr size_t MaxNameLength = 32;

bool isValidName(absl::string_view name) {
  return !name.empty() && name.size() <= MaxNameLength &&
         std::all_of(name.begin(), name.end(), [](char c) {
           return absl::ascii_isalnum(c) || c == '_' || c == '.' || c == '-';
         });
}

absl::Status validate(const std::vector<UpstreamCandidate>& candidates) {
  if (candidates.empty()) {
    return absl::InvalidArgumentError("upstream candidates: empty list");
  }
  absl::flat_hash_set<absl::string_view> names;
  for (const UpstreamCandidate& candidate : candidates) {
    if (!isValidName(candidate.name)) {
      return absl::InvalidArgumentError(
          absl::StrCat("upstream candidates: invalid name '", candidate.name, "'"));
    }
    if (!names.insert(candidate.name).second) {
      return absl::InvalidArgumentError(
          absl::StrCat("upstream candidates: duplicate name '", candidate.name, "'"));
    }
    if (candidate.cluster.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("upstream candidates: '", candidate.name, "' names no cluster"));
    }
    if (!candidate.path.empty() &&
        (candidate.path[0] != '/' || !Http::validHeaderString(candidate.path))) {
      return absl::InvalidArgumentError(
          absl::StrCat("upstream candidates: '", candidate.name, "' has an invalid path"));
    }
  }
  return absl::OkStatus();
}

absl::string_view outcomeName(UpstreamCandidates::Outcome outcome) {
  switch (outcome) {
  case UpstreamCandidates::Outcome::Pending:
    return "pending";
  case UpstreamCandidates::Outcome::Committed:
    return "committed";
  case UpstreamCandidates::Outcome::Rejected:
    return "rejected";
  case UpstreamCandidates::Outcome::NotFollowed:
    return "not_followed";
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

} // namespace

const UpstreamCandidates*
UpstreamCandidates::fromFilterState(const StreamInfo::FilterState& state) {
  return state.getDataReadOnly<UpstreamCandidates>(kFilterStateKey);
}

absl::StatusOr<std::shared_ptr<UpstreamCandidates>>
UpstreamCandidates::create(std::vector<UpstreamCandidate> candidates, std::string added_by) {
  if (absl::Status status = validate(candidates); !status.ok()) {
    return status;
  }
  std::shared_ptr<UpstreamCandidates> decision(new UpstreamCandidates());
  decision->entries_.reserve(candidates.size());
  for (UpstreamCandidate& candidate : candidates) {
    decision->entries_.push_back({std::move(candidate), added_by, "", ""});
  }
  return decision;
}

void UpstreamCandidates::exclude(size_t index, absl::string_view by, absl::string_view reason) {
  ASSERT(index < entries_.size());
  Entry& entry = entries_[index];
  entry.excluded_by = std::string(by);
  entry.excluded_reason = std::string(reason);
}

void UpstreamCandidates::setOutcome(Outcome outcome, std::optional<size_t> chosen) {
  ASSERT((outcome == Outcome::Committed) == chosen.has_value());
  outcome_ = outcome;
  chosen_ = chosen;
}

std::optional<std::string> UpstreamCandidates::serializeAsString() const {
  nlohmann::ordered_json record;
  record["outcome"] = std::string(outcomeName(outcome_));
  if (const Entry* entry = chosen(); entry != nullptr) {
    record["chosen"] = entry->candidate.name;
  }
  nlohmann::ordered_json& candidates = record["candidates"] = nlohmann::ordered_json::array();
  for (const Entry& entry : entries_) {
    nlohmann::ordered_json& out = candidates.emplace_back(nlohmann::ordered_json::object());
    const auto set = [&out](const char* key, const std::string& value) {
      if (!value.empty()) {
        out[key] = value;
      }
    };
    set("name", entry.candidate.name);
    set("cluster", entry.candidate.cluster);
    set("model", entry.candidate.model);
    set("path", entry.candidate.path);
    set("added_by", entry.added_by);
    set("excluded_by", entry.excluded_by);
    set("excluded_reason", entry.excluded_reason);
  }
  // Rewrites come from request-derived filter input, so they may not be valid UTF-8.
  return record.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

StreamInfo::FilterState::Object::FieldType
UpstreamCandidates::getField(absl::string_view field) const {
  if (field == "outcome") {
    return outcomeName(outcome_);
  }
  if (const Entry* entry = chosen(); entry != nullptr && field == "chosen") {
    return absl::string_view(entry->candidate.name);
  }
  return absl::monostate{};
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
