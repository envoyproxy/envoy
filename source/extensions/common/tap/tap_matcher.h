#pragma once

#include "common/http/header_utility.h"

#include "extensions/common/tap/tap.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

/**
 * Factory method to build a matcher given a match config. Calling this function may end
 * up recursively building many matchers, which will all be added to the passed in vector
 * of matchers. See the comments in tap.h for the general structure of how tap matchers work.
 */
void buildMatcher(const envoy::service::tap::v2alpha::MatchPredicate& match_config,
                  std::vector<MatcherPtr>& matchers);

/**
 * Matcher for implementing set logic.
 */
class SetLogicMatcher : public Matcher {
public:
  enum class Type { And, Or };

  SetLogicMatcher(const envoy::service::tap::v2alpha::MatchPredicate::MatchSet& configs,
                  std::vector<MatcherPtr>& matchers, Type type);

  // Extensions::Common::Tap::Matcher
  bool updateMatchStatus(const Http::HeaderMap* request_headers,
                         const Http::HeaderMap* response_headers,
                         std::vector<bool>& statuses) const override;

private:
  std::vector<MatcherPtr>& matchers_;
  std::vector<size_t> indexes_;
  const Type type_;
};

/**
 * Not matcher.
 */
class NotMatcher : public Matcher {
public:
  NotMatcher(const envoy::service::tap::v2alpha::MatchPredicate& config,
             std::vector<MatcherPtr>& matchers);

  // Extensions::Common::Tap::Matcher
  bool updateMatchStatus(const Http::HeaderMap* request_headers,
                         const Http::HeaderMap* response_headers,
                         std::vector<bool>& statuses) const override;

private:
  std::vector<MatcherPtr>& matchers_;
  const size_t not_index_;
};

/**
 * Any matcher (always matches).
 */
class AnyMatcher : public Matcher {
public:
  AnyMatcher(std::vector<MatcherPtr>& matchers) : Matcher(matchers) {}

  // Extensions::Common::Tap::Matcher
  bool updateMatchStatus(const Http::HeaderMap*, const Http::HeaderMap*,
                         std::vector<bool>& statuses) const override {
    statuses[my_index_] = true;
    return true;
  }
};

/**
 * HTTP request matcher.
 */
class HttpRequestMatcher : public Matcher {
public:
  HttpRequestMatcher(const envoy::service::tap::v2alpha::HttpRequestMatch& config,
                     const std::vector<MatcherPtr>& matchers);

  // Extensions::Common::Tap::Matcher
  bool updateMatchStatus(const Http::HeaderMap* request_headers,
                         const Http::HeaderMap* response_headers,
                         std::vector<bool>& statuses) const override;

private:
  std::vector<Http::HeaderUtility::HeaderData> headers_to_match_;
};

/**
 * HTTP response matcher.
 */
class HttpResponseMatcher : public Matcher {
public:
  HttpResponseMatcher(const envoy::service::tap::v2alpha::HttpResponseMatch& config,
                      const std::vector<MatcherPtr>& matchers);

  // Extensions::Common::Tap::Matcher
  bool updateMatchStatus(const Http::HeaderMap* request_headers,
                         const Http::HeaderMap* response_headers,
                         std::vector<bool>& statuses) const override;

private:
  std::vector<Http::HeaderUtility::HeaderData> headers_to_match_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
