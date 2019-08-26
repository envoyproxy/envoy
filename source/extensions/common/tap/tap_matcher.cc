#include "extensions/common/tap/tap_matcher.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

void buildMatcher(const envoy::service::tap::v2alpha::MatchPredicate& match_config,
                  std::vector<MatcherPtr>& matchers) {
  // In order to store indexes and build our matcher tree inline, we must reserve a slot where
  // the matcher we are about to create will go. This allows us to know its future index and still
  // construct more of the tree in each called constructor (e.g., multiple OR/AND conditions).
  // Once fully constructed, we move the matcher into its position below. See the tap matcher
  // overview in tap.h for more information.
  matchers.emplace_back(nullptr);

  MatcherPtr new_matcher;
  switch (match_config.rule_case()) {
  case envoy::service::tap::v2alpha::MatchPredicate::kOrMatch:
    new_matcher = std::make_unique<SetLogicMatcher>(match_config.or_match(), matchers,
                                                    SetLogicMatcher::Type::Or);
    break;
  case envoy::service::tap::v2alpha::MatchPredicate::kAndMatch:
    new_matcher = std::make_unique<SetLogicMatcher>(match_config.and_match(), matchers,
                                                    SetLogicMatcher::Type::And);
    break;
  case envoy::service::tap::v2alpha::MatchPredicate::kNotMatch:
    new_matcher = std::make_unique<NotMatcher>(match_config.not_match(), matchers);
    break;
  case envoy::service::tap::v2alpha::MatchPredicate::kAnyMatch:
    new_matcher = std::make_unique<AnyMatcher>(matchers);
    break;
  case envoy::service::tap::v2alpha::MatchPredicate::kHttpRequestHeadersMatch:
    new_matcher = std::make_unique<HttpRequestHeadersMatcher>(
        match_config.http_request_headers_match(), matchers);
    break;
  case envoy::service::tap::v2alpha::MatchPredicate::kHttpRequestTrailersMatch:
    new_matcher = std::make_unique<HttpRequestTrailersMatcher>(
        match_config.http_request_trailers_match(), matchers);
    break;
  case envoy::service::tap::v2alpha::MatchPredicate::kHttpResponseHeadersMatch:
    new_matcher = std::make_unique<HttpResponseHeadersMatcher>(
        match_config.http_response_headers_match(), matchers);
    break;
  case envoy::service::tap::v2alpha::MatchPredicate::kHttpResponseTrailersMatch:
    new_matcher = std::make_unique<HttpResponseTrailersMatcher>(
        match_config.http_response_trailers_match(), matchers);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  // Per above, move the matcher into its position.
  matchers[new_matcher->index()] = std::move(new_matcher);
}

SetLogicMatcher::SetLogicMatcher(
    const envoy::service::tap::v2alpha::MatchPredicate::MatchSet& configs,
    std::vector<MatcherPtr>& matchers, Type type)
    : LogicMatcherBase(matchers), matchers_(matchers), type_(type) {
  for (const auto& config : configs.rules()) {
    indexes_.push_back(matchers_.size());
    buildMatcher(config, matchers_);
  }
}

void SetLogicMatcher::updateLocalStatus(MatchStatusVector& statuses,
                                        const UpdateFunctor& functor) const {
  if (!statuses[my_index_].might_change_status_) {
    return;
  }

  for (size_t index : indexes_) {
    functor(*matchers_[index], statuses);
  }

  auto predicate = [&statuses](size_t index) { return statuses[index].matches_; };
  if (type_ == Type::And) {
    statuses[my_index_].matches_ = std::all_of(indexes_.begin(), indexes_.end(), predicate);
  } else {
    ASSERT(type_ == Type::Or);
    statuses[my_index_].matches_ = std::any_of(indexes_.begin(), indexes_.end(), predicate);
  }

  // TODO(mattklein123): We can potentially short circuit this even further if we git a single false
  // in an AND set or a single true in an OR set.
  statuses[my_index_].might_change_status_ =
      std::any_of(indexes_.begin(), indexes_.end(),
                  [&statuses](size_t index) { return statuses[index].might_change_status_; });
}

NotMatcher::NotMatcher(const envoy::service::tap::v2alpha::MatchPredicate& config,
                       std::vector<MatcherPtr>& matchers)
    : LogicMatcherBase(matchers), matchers_(matchers), not_index_(matchers.size()) {
  buildMatcher(config, matchers);
}

void NotMatcher::updateLocalStatus(MatchStatusVector& statuses,
                                   const UpdateFunctor& functor) const {
  if (!statuses[my_index_].might_change_status_) {
    return;
  }

  functor(*matchers_[not_index_], statuses);
  statuses[my_index_].matches_ = !statuses[not_index_].matches_;
  statuses[my_index_].might_change_status_ = statuses[not_index_].might_change_status_;
}

HttpHeaderMatcherBase::HttpHeaderMatcherBase(
    const envoy::service::tap::v2alpha::HttpHeadersMatch& config,
    const std::vector<MatcherPtr>& matchers)
    : SimpleMatcher(matchers),
      headers_to_match_(Http::HeaderUtility::buildHeaderDataVector(config.headers())) {}

void HttpHeaderMatcherBase::matchHeaders(const Http::HeaderMap& headers,
                                         MatchStatusVector& statuses) const {
  ASSERT(statuses[my_index_].might_change_status_);
  statuses[my_index_].matches_ = Http::HeaderUtility::matchHeaders(headers, headers_to_match_);
  statuses[my_index_].might_change_status_ = false;
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
