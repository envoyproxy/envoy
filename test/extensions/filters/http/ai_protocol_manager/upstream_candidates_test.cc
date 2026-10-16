#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/upstream_candidates.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class UpstreamCandidatesPeer {
public:
  static std::shared_ptr<UpstreamCandidates> create(std::vector<UpstreamCandidate> candidates,
                                                    std::string added_by) {
    return UpstreamCandidates::create(std::move(candidates), std::move(added_by)).value();
  }
  static void exclude(UpstreamCandidates& decision, size_t index, absl::string_view reason) {
    decision.exclude(index, "ai_protocol_manager", reason);
  }
  static void setOutcome(UpstreamCandidates& decision, UpstreamCandidates::Outcome outcome,
                         std::optional<size_t> chosen = std::nullopt) {
    decision.setOutcome(outcome, chosen);
  }
};

namespace {

using Outcome = UpstreamCandidates::Outcome;

std::shared_ptr<UpstreamCandidates> decision() {
  return UpstreamCandidatesPeer::create(
      {{"a", "cluster_a", "", ""}, {"b", "cluster_b", "model-b", "/b"}}, "f/rule");
}

std::string outcomeField(const UpstreamCandidates& decision) {
  return std::string(absl::get<absl::string_view>(decision.getField("outcome")));
}

TEST(UpstreamCandidatesTest, SerializesAPendingDecisionWithoutEmptyFields) {
  EXPECT_EQ(decision()->serializeAsString(),
            R"({"outcome":"pending","candidates":[)"
            R"({"name":"a","cluster":"cluster_a","added_by":"f/rule"},)"
            R"({"name":"b","cluster":"cluster_b","model":"model-b","path":"/b",)"
            R"("added_by":"f/rule"}]})");
}

TEST(UpstreamCandidatesTest, SerializesACommittedDecisionWithExclusions) {
  std::shared_ptr<UpstreamCandidates> committed = decision();
  UpstreamCandidatesPeer::exclude(*committed, 0, "unknown_cluster");
  UpstreamCandidatesPeer::setOutcome(*committed, Outcome::Committed, 1);

  EXPECT_EQ(committed->outcome(), Outcome::Committed);
  ASSERT_NE(committed->chosen(), nullptr);
  EXPECT_EQ(committed->chosen()->candidate.name, "b");
  EXPECT_EQ(committed->serializeAsString(),
            R"({"outcome":"committed","chosen":"b","candidates":[)"
            R"({"name":"a","cluster":"cluster_a","added_by":"f/rule",)"
            R"("excluded_by":"ai_protocol_manager","excluded_reason":"unknown_cluster"},)"
            R"({"name":"b","cluster":"cluster_b","model":"model-b","path":"/b",)"
            R"("added_by":"f/rule"}]})");
}

TEST(UpstreamCandidatesTest, SerializesInvalidUtf8AsReplacementCharacters) {
  std::shared_ptr<UpstreamCandidates> invalid =
      UpstreamCandidatesPeer::create({{"a", "cluster_a", "\xff", ""}}, "f");
  EXPECT_THAT(invalid->serializeAsString().value(),
              testing::HasSubstr("\"model\":\"\xEF\xBF\xBD\""));
}

TEST(UpstreamCandidatesTest, FieldsNameTheOutcomeAndTheChosenCandidate) {
  std::shared_ptr<UpstreamCandidates> record = decision();
  EXPECT_TRUE(record->hasFieldSupport());
  EXPECT_EQ(outcomeField(*record), "pending");
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(record->getField("chosen")));
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(record->getField("unknown")));

  UpstreamCandidatesPeer::setOutcome(*record, Outcome::Rejected);
  EXPECT_EQ(outcomeField(*record), "rejected");
  EXPECT_EQ(record->chosen(), nullptr);
  UpstreamCandidatesPeer::setOutcome(*record, Outcome::NotFollowed);
  EXPECT_EQ(outcomeField(*record), "not_followed");
  UpstreamCandidatesPeer::setOutcome(*record, Outcome::Committed, 0);
  EXPECT_EQ(outcomeField(*record), "committed");
  EXPECT_EQ(absl::get<absl::string_view>(record->getField("chosen")), "a");
}

TEST(UpstreamCandidatesTest, FromFilterStateIsTypeChecked) {
  StreamInfo::FilterStateImpl state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(UpstreamCandidates::fromFilterState(state), nullptr);

  state.setData(UpstreamCandidates::kFilterStateKey,
                std::make_shared<Router::StringAccessorImpl>("forged"));
  EXPECT_EQ(UpstreamCandidates::fromFilterState(state), nullptr);

  StreamInfo::FilterStateImpl published(StreamInfo::FilterState::LifeSpan::FilterChain);
  std::shared_ptr<UpstreamCandidates> record = decision();
  published.setData(UpstreamCandidates::kFilterStateKey, record);
  EXPECT_EQ(UpstreamCandidates::fromFilterState(published), record.get());
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
