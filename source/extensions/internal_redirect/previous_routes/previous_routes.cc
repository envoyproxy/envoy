#include "source/extensions/internal_redirect/previous_routes/previous_routes.h"

#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

namespace {

constexpr absl::string_view PreviousRoutesPredicateStateNamePrefix =
    "envoy.internal_redirect.previous_routes_predicate_state";

class PreviousRoutesPredicateState : public StreamInfo::FilterState::Object {
public:
  PreviousRoutesPredicateState() = default;
  // Disallow copy so that we don't accidentally take a copy of the state
  // through FilterState::getDataMutable, which will cause confusing bug that
  // states are not updated in the original copy.
  PreviousRoutesPredicateState(const PreviousRoutesPredicateState&) = delete;
  PreviousRoutesPredicateState& operator=(const PreviousRoutesPredicateState&) = delete;

  bool insertRouteIfNotPresent(absl::string_view route) {
    return previous_routes_.insert(std::string(route)).second;
  }

private:
  absl::flat_hash_set<std::string> previous_routes_;
};

} // namespace

bool PreviousRoutesPredicate::acceptTargetRoute(StreamInfo::FilterState& filter_state,
                                                absl::string_view route_name, bool, bool) {
  auto filter_state_name =
      absl::StrCat(PreviousRoutesPredicateStateNamePrefix, ".", current_route_name_);
  if (!filter_state.hasData<PreviousRoutesPredicateState>(filter_state_name)) {
    filter_state.setData(filter_state_name, std::make_unique<PreviousRoutesPredicateState>(),
                         StreamInfo::FilterState::StateType::Mutable,
                         StreamInfo::FilterState::LifeSpan::Request);
  }
  auto predicate_state =
      filter_state.getDataMutable<PreviousRoutesPredicateState>(filter_state_name);
  return predicate_state->insertRouteIfNotPresent(route_name);
}

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
