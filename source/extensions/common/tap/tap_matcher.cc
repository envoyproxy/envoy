#include "extensions/common/tap/tap_matcher.h"

#include "envoy/config/tap/v3/common.pb.h"

#include "common/common/assert.h"
#include "common/common/hex.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

void buildMatcher(const envoy::config::tap::v3::MatchPredicate& match_config,
                  std::vector<MatcherPtr>& matchers) {
  // In order to store indexes and build our matcher tree inline, we must reserve a slot where
  // the matcher we are about to create will go. This allows us to know its future index and still
  // construct more of the tree in each called constructor (e.g., multiple OR/AND conditions).
  // Once fully constructed, we move the matcher into its position below. See the tap matcher
  // overview in tap.h for more information.
  matchers.emplace_back(nullptr);

  MatcherPtr new_matcher;
  switch (match_config.rule_case()) {
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kOrMatch:
    new_matcher = std::make_unique<SetLogicMatcher>(match_config.or_match(), matchers,
                                                    SetLogicMatcher::Type::Or);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kAndMatch:
    new_matcher = std::make_unique<SetLogicMatcher>(match_config.and_match(), matchers,
                                                    SetLogicMatcher::Type::And);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kNotMatch:
    new_matcher = std::make_unique<NotMatcher>(match_config.not_match(), matchers);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kAnyMatch:
    new_matcher = std::make_unique<AnyMatcher>(matchers);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kHttpRequestHeadersMatch:
    new_matcher = std::make_unique<HttpRequestHeadersMatcher>(
        match_config.http_request_headers_match(), matchers);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kHttpRequestTrailersMatch:
    new_matcher = std::make_unique<HttpRequestTrailersMatcher>(
        match_config.http_request_trailers_match(), matchers);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kHttpResponseHeadersMatch:
    new_matcher = std::make_unique<HttpResponseHeadersMatcher>(
        match_config.http_response_headers_match(), matchers);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kHttpResponseTrailersMatch:
    new_matcher = std::make_unique<HttpResponseTrailersMatcher>(
        match_config.http_response_trailers_match(), matchers);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kHttpRequestGenericBodyMatch:
    new_matcher = std::make_unique<HttpRequestGenericBodyMatcher>(
        match_config.http_request_generic_body_match(), matchers);
    break;
  case envoy::config::tap::v3::MatchPredicate::RuleCase::kHttpResponseGenericBodyMatch:
    new_matcher = std::make_unique<HttpResponseGenericBodyMatcher>(
        match_config.http_response_generic_body_match(), matchers);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  // Per above, move the matcher into its position.
  matchers[new_matcher->index()] = std::move(new_matcher);
}

SetLogicMatcher::SetLogicMatcher(const envoy::config::tap::v3::MatchPredicate::MatchSet& configs,
                                 std::vector<MatcherPtr>& matchers, Type type)
    : LogicMatcherBase(matchers), matchers_(matchers), type_(type) {
  for (const auto& config : configs.rules()) {
    printf("SETTING LOGIC MATCHER: %d\n", static_cast<int>(matchers_.size()));
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

NotMatcher::NotMatcher(const envoy::config::tap::v3::MatchPredicate& config,
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

HttpHeaderMatcherBase::HttpHeaderMatcherBase(const envoy::config::tap::v3::HttpHeadersMatch& config,
                                             const std::vector<MatcherPtr>& matchers)
    : SimpleMatcher(matchers),
      headers_to_match_(Http::HeaderUtility::buildHeaderDataVector(config.headers())) {}

void HttpHeaderMatcherBase::matchHeaders(const Http::HeaderMap& headers,
                                         MatchStatusVector& statuses) const {
  ASSERT(statuses[my_index_].might_change_status_);
  statuses[my_index_].matches_ = Http::HeaderUtility::matchHeaders(headers, headers_to_match_);
  statuses[my_index_].might_change_status_ = false;
}

HttpGenericBodyMatcher::HttpGenericBodyMatcher(
    const envoy::config::tap::v3::HttpGenericBodyMatch& config,
    const std::vector<MatcherPtr>& matchers)
    : HttpBodyMatcherBase(matchers) {
  for (auto i : config.patterns()) {
    switch (i.rule_case()) {
    case envoy::config::tap::v3::HttpGenericBodyMatch::GenericTextMatch::kContainsHex: {
      // convert the hex string to real hex values
      // string containing "01" will be converted to 1 byte: 0x01
      const std::vector<unsigned char> hex = Hex::decode(i.contains_hex());
      if (hex.empty()) {
        throw EnvoyException(fmt::format("invalid hex string '{}'", i.contains_hex()));
      }
      std::string hex_string;
      hex_string.assign(reinterpret_cast<const char*>(hex.data()), hex.size());
      patterns_.push_back(hex_string);
    } break;
    case envoy::config::tap::v3::HttpGenericBodyMatch::GenericTextMatch::kContainsText:
      patterns_.push_back(i.contains_text());
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  limit_ = config.bytes_limit();
}

void HttpGenericBodyMatcher::onBody(const Buffer::Instance& data,
                                    MatchStatusVector& statuses) const {
  bool found = std::all_of(patterns_.begin(), patterns_.end(), [&data, this](std::string pattern) {
    return (-1 !=
            data.search(static_cast<const void*>(pattern.c_str()), pattern.length(), 0, limit_));
  });

  statuses[my_index_].matches_ = found;
  statuses[my_index_].might_change_status_ = false;
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
