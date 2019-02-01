#pragma once

#include "envoy/service/tap/v2alpha/common.pb.h"

#include "common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

class Matcher;
using MatcherPtr = std::unique_ptr<Matcher>;

/**
 * Base class for all tap matchers.
 *
 * A high level note on the design of tap matching which is different from other matching in Envoy
 * due to a requirement to support streaming matching (match as new data arrives versus
 * calculating the match given all available data at once).
 * - The matching system is composed of a constant matching configuration. This is essentially
 *   a tree of matchers given logical AND, OR, NOT, etc.
 * - A per-stream/request matching status must be kept in order to compute interim match status.
 * - In order to make this computationally efficient, the matching tree is kept in a vector, with
 *   all references to other matchers implemented using an index into the vector. The vector is
 *   effectively a preorder traversal flattened N-ary tree.
 * - The previous point allows the creation of a per-stream/request vector of booleans of the same
 *   size as the matcher vector. Then, when match status is updated given new information, the
 *   vector of booleans can be easily updated using the same indexes as in the constant match
 *   configuration.
 * - Finally, a matches() function can be trivially implemented by looking in the status vector at
 *   the index position that the current matcher is located in.
 *
 * TODO(mattklein123): Currently, any match updates perform a recursive call on any child match
 * nodes. It's possible that we can short circuit this in certain cases but this needs more
 * thinking (e.g., if an OR matcher already has one match and it's not possible for a matcher to
 * flip from true to false).
 */
class Matcher {
public:
  virtual ~Matcher() = default;

  /**
   * @return the matcher's index in the match tree vector (see above).
   */
  size_t index() { return my_index_; }

  /**
   * Update match status when a stream is created. This might be an HTTP stream, a TCP connectin,
   * etc. This allows any matchers to flip to an initial state of true if applicable.
   */
  virtual bool onNewStream(std::vector<bool>& statuses) const PURE;

  /**
   * Update match status given HTTP request headers.
   * @param request_headers supplies the request headers.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual bool onHttpRequestHeaders(const Http::HeaderMap& request_headers,
                                    std::vector<bool>& statuses) const PURE;
  /**
   * Update match status given HTTP response headers.
   * @param response_headers supplies the response headers.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual bool onHttpResponseHeaders(const Http::HeaderMap& response_headers,
                                     std::vector<bool>& statuses) const PURE;

  /**
   * @return whether given currently available information, the matcher matches.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  bool matches(const std::vector<bool>& statuses) const { return statuses[my_index_]; }

protected:
  /**
   * Base class constructor for a matcher.
   * @param matchers supplies the match tree vector being built.
   */
  Matcher(const std::vector<MatcherPtr>& matchers)
      // NOTE: This code assumes that the index for the matcher being constructed has already been
      // allocated, which is why my_index_ is set to size() - 1. See buildMatcher() in
      // tap_matcher.cc.
      : my_index_(matchers.size() - 1) {}

  const size_t my_index_;
};

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
  bool onNewStream(std::vector<bool>& statuses) const override;
  bool onHttpRequestHeaders(const Http::HeaderMap& request_headers,
                            std::vector<bool>& statuses) const override;
  bool onHttpResponseHeaders(const Http::HeaderMap& response_headers,
                             std::vector<bool>& statuses) const override;

private:
  bool updateLocalStatus(std::vector<bool>& statuses) const;

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
  bool onNewStream(std::vector<bool>& statuses) const override;
  bool onHttpRequestHeaders(const Http::HeaderMap& request_headers,
                            std::vector<bool>& statuses) const override;
  bool onHttpResponseHeaders(const Http::HeaderMap& response_headers,
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
  bool onNewStream(std::vector<bool>& statuses) const override {
    statuses[my_index_] = true;
    return true;
  }
  bool onHttpRequestHeaders(const Http::HeaderMap&, std::vector<bool>&) const override {
    return true;
  }
  bool onHttpResponseHeaders(const Http::HeaderMap&, std::vector<bool>&) const override {
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
  bool onNewStream(std::vector<bool>&) const override { return false; }
  bool onHttpRequestHeaders(const Http::HeaderMap& request_headers,
                            std::vector<bool>& statuses) const override;
  bool onHttpResponseHeaders(const Http::HeaderMap&, std::vector<bool>& statuses) const override {
    return statuses[my_index_];
  }

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
  bool onNewStream(std::vector<bool>&) const override { return false; }
  bool onHttpRequestHeaders(const Http::HeaderMap&, std::vector<bool>&) const override {
    return false;
  }
  bool onHttpResponseHeaders(const Http::HeaderMap& response_headers,
                             std::vector<bool>& statuses) const override;

private:
  std::vector<Http::HeaderUtility::HeaderData> headers_to_match_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
