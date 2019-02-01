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
  case envoy::service::tap::v2alpha::MatchPredicate::kHttpRequestMatch:
    new_matcher = std::make_unique<HttpRequestMatcher>(match_config.http_request_match(), matchers);
    break;
  case envoy::service::tap::v2alpha::MatchPredicate::kHttpResponseMatch:
    new_matcher =
        std::make_unique<HttpResponseMatcher>(match_config.http_response_match(), matchers);
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
    : Matcher(matchers), matchers_(matchers), type_(type) {
  for (const auto& config : configs.rules()) {
    indexes_.push_back(matchers_.size());
    buildMatcher(config, matchers_);
  }
}

bool SetLogicMatcher::updateLocalStatus(std::vector<bool>& statuses) const {
  auto predicate = [&statuses](size_t index) { return statuses[index]; };
  if (type_ == Type::And) {
    statuses[my_index_] = std::all_of(indexes_.begin(), indexes_.end(), predicate);
  } else {
    ASSERT(type_ == Type::Or);
    statuses[my_index_] = std::any_of(indexes_.begin(), indexes_.end(), predicate);
  }

  return statuses[my_index_];
}

bool SetLogicMatcher::onNewStream(std::vector<bool>& statuses) const {
  for (size_t index : indexes_) {
    statuses[index] = matchers_[index]->onNewStream(statuses);
  }

  return updateLocalStatus(statuses);
}

bool SetLogicMatcher::onHttpRequestHeaders(const Http::HeaderMap& request_headers,
                                           std::vector<bool>& statuses) const {
  for (size_t index : indexes_) {
    statuses[index] = matchers_[index]->onHttpRequestHeaders(request_headers, statuses);
  }

  return updateLocalStatus(statuses);
}

bool SetLogicMatcher::onHttpResponseHeaders(const Http::HeaderMap& response_headers,
                                            std::vector<bool>& statuses) const {
  for (size_t index : indexes_) {
    statuses[index] = matchers_[index]->onHttpResponseHeaders(response_headers, statuses);
  }

  return updateLocalStatus(statuses);
}

NotMatcher::NotMatcher(const envoy::service::tap::v2alpha::MatchPredicate& config,
                       std::vector<MatcherPtr>& matchers)
    : Matcher(matchers), matchers_(matchers), not_index_(matchers.size()) {
  buildMatcher(config, matchers);
}

bool NotMatcher::onNewStream(std::vector<bool>& statuses) const {
  statuses[my_index_] = !matchers_[not_index_]->onNewStream(statuses);
  return statuses[my_index_];
}

bool NotMatcher::onHttpRequestHeaders(const Http::HeaderMap& request_headers,
                                      std::vector<bool>& statuses) const {
  statuses[my_index_] = !matchers_[not_index_]->onHttpRequestHeaders(request_headers, statuses);
  return statuses[my_index_];
}

bool NotMatcher::onHttpResponseHeaders(const Http::HeaderMap& response_headers,
                                       std::vector<bool>& statuses) const {
  statuses[my_index_] = !matchers_[not_index_]->onHttpResponseHeaders(response_headers, statuses);
  return statuses[my_index_];
}

HttpRequestMatcher::HttpRequestMatcher(const envoy::service::tap::v2alpha::HttpRequestMatch& config,
                                       const std::vector<MatcherPtr>& matchers)
    : Matcher(matchers) {
  for (const auto& header_match : config.headers()) {
    headers_to_match_.emplace_back(header_match);
  }
}

bool HttpRequestMatcher::onHttpRequestHeaders(const Http::HeaderMap& request_headers,
                                              std::vector<bool>& statuses) const {
  statuses[my_index_] = Http::HeaderUtility::matchHeaders(request_headers, headers_to_match_);
  return statuses[my_index_];
}

HttpResponseMatcher::HttpResponseMatcher(
    const envoy::service::tap::v2alpha::HttpResponseMatch& config,
    const std::vector<MatcherPtr>& matchers)
    : Matcher(matchers) {
  for (const auto& header_match : config.headers()) {
    headers_to_match_.emplace_back(header_match);
  }
}

bool HttpResponseMatcher::onHttpResponseHeaders(const Http::HeaderMap& response_headers,
                                                std::vector<bool>& statuses) const {
  statuses[my_index_] = Http::HeaderUtility::matchHeaders(response_headers, headers_to_match_);
  return statuses[my_index_];
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
