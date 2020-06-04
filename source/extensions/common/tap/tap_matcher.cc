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

// HttpGenericBodyMatcher
// Scans the HTTP body and looks for patterns.
// HTTP body may be passed to the matcher in chunks. The search logic buffers
// only as many bytes as is the length of the longest pattern to be found.
HttpGenericBodyMatcher::HttpGenericBodyMatcher(
    const envoy::config::tap::v3::HttpGenericBodyMatch& config,
    const std::vector<MatcherPtr>& matchers)
    : HttpBodyMatcherBase(matchers) {
  for (const auto& i : config.patterns()) {
    switch (i.rule_case()) {
    case envoy::config::tap::v3::HttpGenericBodyMatch::GenericTextMatch::kContainsHex: {
      // Convert the hex string to real hex values.
      // String containing "01" will be converted to 1 byte: 0x01
      const std::vector<unsigned char> hex = Hex::decode(i.contains_hex());
      if (hex.empty()) {
        throw EnvoyException(fmt::format("invalid hex string '{}'", i.contains_hex()));
      }
      std::string hex_string;
      hex_string.assign(reinterpret_cast<const char*>(hex.data()), hex.size());
      patterns_.push_back(hex_string);
    } break;
    // For text pattern just add the string to vector of patterns the matcher will look for.
    case envoy::config::tap::v3::HttpGenericBodyMatch::GenericTextMatch::kContainsText:
      patterns_.push_back(i.contains_text());
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  limit_ = config.bytes_limit();
  auto index = std::max_element(
      patterns_.begin(), patterns_.end(),
      [](const std::string& i, const std::string& j) -> bool { return i.length() < j.length(); });
  overlap_size_ = (*index).length() - 1;
  overlap_ = std::make_unique<char[]>(overlap_size_);
}

void HttpGenericBodyMatcher::onBody(const Buffer::Instance& data, MatchStatusVector& statuses) {
  if (((0 != limit_) && (limit_ == processed_bytes_)) || (patterns_.empty())) {
    // End of search limit has been already reached or all patterns have been found.
    // Status is not going to change.
    ASSERT(statuses[my_index_].might_change_status_ == false);
    return;
  }
  if (0 != bytes_in_overlap_) {
    // Here we handle a situation when a pattern is spread across multiple body buffers.
    // overlap_ stores number of bytes from previous body chunks equal to longest pattern yet to be
    // found minus one byte (-1). The logic below tries to find the beginning of the pattern in
    // overlap_ buffer and the pattern should continue at the beginning of the next buffer. Iterate
    // through all patterns yet to be find.
    auto it = patterns_.begin();
    while (it != patterns_.end()) {
      const auto& pattern = *it;
      // Take the first character from the pattern and locate it in overlap_.
      auto index_pattern = 0;
      auto first_char =
          static_cast<char*>(memchr(overlap_.get(), pattern[index_pattern], bytes_in_overlap_));

      if (first_char == nullptr) {
        // Pattern not found. Check the next one.
        it++;
        continue;
      }
      // Calculate the offset of the found character
      // from the beginning of the overlap_ buffer.
      size_t index_overlap = first_char - overlap_.get();
      bool match = true;
      // Continue checking characters until end of overlap_ buffer.
      while (index_overlap < bytes_in_overlap_) {
        if (pattern[index_pattern] != overlap_[index_overlap]) {
          match = false;
          break;
        }
        index_pattern++;
        index_overlap++;
      }
      if (!match) {
        // Pattern not found. Try the next pattern.
        it++;
        continue;
      }
      // Now check if the remaining of the pattern matches the beginning of the body
      // buffer.i Do it only if there is sufficient number of bytes in the data buffer.
      auto pattern_remainder = pattern.substr(index_pattern);
      if ((0 != limit_) && (pattern_remainder.length() > (limit_ - processed_bytes_))) {
        // Even if we got match it would be outside the search limit
        it++;
        continue;
      }
      if ((pattern_remainder.length() <= data.length()) && data.startsWith(pattern_remainder)) {
        // Pattern found. Remove it from the list of patterns to be found.
        // If it was the last one of the patterns to be found update
        // matcher's state.
        it = patterns_.erase(it);
      } else {
        it++;
      }
    }
  }

  auto search_limit = limit_ - processed_bytes_;
  for (auto it = patterns_.begin(); it != patterns_.end();) {
    if (-1 != data.search(static_cast<const void*>(it->data()), it->length(), 0, search_limit)) {
      it = patterns_.erase(it);
    } else {
      it++;
    }
  }

  if (patterns_.empty()) {
    // All patterns were found.
    statuses[my_index_].matches_ = true;
    statuses[my_index_].might_change_status_ = false;
    return;
  }

  if (0 != limit_) {
    processed_bytes_ = std::min(uint64_t(limit_), processed_bytes_ + data.length());
    if (limit_ == processed_bytes_) {
      // End of search limit has been reached and not all patterns have been found.
      statuses[my_index_].matches_ = false;
      statuses[my_index_].might_change_status_ = false;
      return;
    }
  }

  // The matcher buffers the last seen X bytes where X is equal to the length of the
  // longest pattern - 1. With the arrival of the new 'data' the following situations
  // are possible:
  // 1. The new data's length is larger or equal to X. In this case just copy last X bytes
  // from the data to overlap_ buffer.
  // 2. The new data length is smaller than X and there enough room in overlap buffer to just copy
  // the bytes from data.
  // 3. The new data length is smaller than X and there is not enough room in overlap buffer.
  if (data.length() >= overlap_size_) {
    // Case 1:
    // Just overwrite the entire overlap_ buffer with new data.
    data.copyOut(data.length() - overlap_size_, overlap_size_, const_cast<char*>(overlap_.get()));
    bytes_in_overlap_ = overlap_size_;
  } else {
    if (data.length() <= (overlap_size_ - bytes_in_overlap_)) {
      // Case 2. Just add the new data on top of already buffered.
      data.copyOut(0, data.length(), overlap_.get() + bytes_in_overlap_);
      bytes_in_overlap_ += data.length();
    } else {
      // Case 3. First shift data to make room for new data and then copy
      // entire new buffer.
      const size_t shift = bytes_in_overlap_ - (overlap_size_ - data.length());
      memcpy(overlap_.get(), overlap_.get() + shift, (bytes_in_overlap_ - shift));
      data.copyOut(0, data.length(), overlap_.get() + (bytes_in_overlap_ - shift));
      bytes_in_overlap_ += data.length() - shift;
      ASSERT(bytes_in_overlap_ == overlap_size_);
    }
  }
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
