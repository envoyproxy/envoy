#pragma once

#include "envoy/config/tap/v3/common.pb.h"

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
 * - The previous point allows the creation of a per-stream/request vector of match statuses of
 *   the same size as the matcher vector. Then, when match status is updated given new
 *   information, the vector of match statuses can be easily updated using the same indexes as in
 *   the constant match configuration.
 * - Finally, a matches() function can be trivially implemented by looking in the status vector at
 *   the index position that the current matcher is located in.
 */
class Matcher {
public:
  struct MatchStatus {
    bool operator==(const MatchStatus& rhs) const {
      return matches_ == rhs.matches_ && might_change_status_ == rhs.might_change_status_;
    }

    bool matches_{false};            // Does the matcher currently match?
    bool might_change_status_{true}; // Is it possible for matches_ to change in subsequent updates?
  };

  using MatchStatusVector = std::vector<MatchStatus>;

  /**
   * Base class constructor for a matcher.
   * @param matchers supplies the match tree vector being built.
   */
  Matcher(const std::vector<MatcherPtr>& matchers)
      // NOTE: This code assumes that the index for the matcher being constructed has already been
      // allocated, which is why my_index_ is set to size() - 1. See buildMatcher() in
      // tap_matcher.cc.
      : my_index_(matchers.size() - 1) {}

  virtual ~Matcher() = default;

  /**
   * @return the matcher's index in the match tree vector (see above).
   */
  size_t index() { return my_index_; }

  /**
   * Update match status when a stream is created. This might be an HTTP stream, a TCP connection,
   * etc. This allows any matchers to flip to an initial state of true if applicable.
   */
  virtual void onNewStream(MatchStatusVector& statuses) const PURE;

  /**
   * Update match status given HTTP request headers.
   * @param request_headers supplies the request headers.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual void onHttpRequestHeaders(const Http::RequestHeaderMap& request_headers,
                                    MatchStatusVector& statuses) const PURE;

  /**
   * Update match status given HTTP request trailers.
   * @param request_trailers supplies the request trailers.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual void onHttpRequestTrailers(const Http::RequestTrailerMap& request_trailers,
                                     MatchStatusVector& statuses) const PURE;

  /**
   * Update match status given HTTP response headers.
   * @param response_headers supplies the response headers.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual void onHttpResponseHeaders(const Http::ResponseHeaderMap& response_headers,
                                     MatchStatusVector& statuses) const PURE;

  /**
   * Update match status given HTTP response trailers.
   * @param response_headers supplies the response trailers.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual void onHttpResponseTrailers(const Http::ResponseTrailerMap& response_trailers,
                                      MatchStatusVector& statuses) const PURE;

  /**
   * @return whether given currently available information, the matcher matches.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  MatchStatus matchStatus(const MatchStatusVector& statuses) const { return statuses[my_index_]; }

protected:
  const size_t my_index_;
};

/**
 * Factory method to build a matcher given a match config. Calling this function may end
 * up recursively building many matchers, which will all be added to the passed in vector
 * of matchers. See the comments in tap.h for the general structure of how tap matchers work.
 */
void buildMatcher(const envoy::config::tap::v3::MatchPredicate& match_config,
                  std::vector<MatcherPtr>& matchers);

/**
 * Base class for logic matchers that need to forward update calls to child matchers.
 */
class LogicMatcherBase : public Matcher {
public:
  using Matcher::Matcher;

  // Extensions::Common::Tap::Matcher
  void onNewStream(MatchStatusVector& statuses) const override {
    updateLocalStatus(statuses,
                      [](Matcher& m, MatchStatusVector& statuses) { m.onNewStream(statuses); });
  }
  void onHttpRequestHeaders(const Http::RequestHeaderMap& request_headers,
                            MatchStatusVector& statuses) const override {
    updateLocalStatus(statuses, [&request_headers](Matcher& m, MatchStatusVector& statuses) {
      m.onHttpRequestHeaders(request_headers, statuses);
    });
  }
  void onHttpRequestTrailers(const Http::RequestTrailerMap& request_trailers,
                             MatchStatusVector& statuses) const override {
    updateLocalStatus(statuses, [&request_trailers](Matcher& m, MatchStatusVector& statuses) {
      m.onHttpRequestTrailers(request_trailers, statuses);
    });
  }
  void onHttpResponseHeaders(const Http::ResponseHeaderMap& response_headers,
                             MatchStatusVector& statuses) const override {
    updateLocalStatus(statuses, [&response_headers](Matcher& m, MatchStatusVector& statuses) {
      m.onHttpResponseHeaders(response_headers, statuses);
    });
  }
  void onHttpResponseTrailers(const Http::ResponseTrailerMap& response_trailers,
                              MatchStatusVector& statuses) const override {
    updateLocalStatus(statuses, [&response_trailers](Matcher& m, MatchStatusVector& statuses) {
      m.onHttpResponseTrailers(response_trailers, statuses);
    });
  }

protected:
  using UpdateFunctor = std::function<void(Matcher&, MatchStatusVector&)>;
  virtual void updateLocalStatus(MatchStatusVector& statuses,
                                 const UpdateFunctor& functor) const PURE;
};

/**
 * Matcher for implementing set logic.
 */
class SetLogicMatcher : public LogicMatcherBase {
public:
  enum class Type { And, Or };

  SetLogicMatcher(const envoy::config::tap::v3::MatchPredicate::MatchSet& configs,
                  std::vector<MatcherPtr>& matchers, Type type);

private:
  void updateLocalStatus(MatchStatusVector& statuses, const UpdateFunctor& functor) const override;

  std::vector<MatcherPtr>& matchers_;
  std::vector<size_t> indexes_;
  const Type type_;
};

/**
 * Not matcher.
 */
class NotMatcher : public LogicMatcherBase {
public:
  NotMatcher(const envoy::config::tap::v3::MatchPredicate& config,
             std::vector<MatcherPtr>& matchers);

private:
  void updateLocalStatus(MatchStatusVector& statuses, const UpdateFunctor& functor) const override;

  std::vector<MatcherPtr>& matchers_;
  const size_t not_index_;
};

/**
 * A base class for a matcher that generally wants to return default values, but might override
 * a single update function.
 */
class SimpleMatcher : public Matcher {
public:
  using Matcher::Matcher;

  // Extensions::Common::Tap::Matcher
  void onNewStream(MatchStatusVector&) const override {}
  void onHttpRequestHeaders(const Http::RequestHeaderMap&, MatchStatusVector&) const override {}
  void onHttpRequestTrailers(const Http::RequestTrailerMap&, MatchStatusVector&) const override {}
  void onHttpResponseHeaders(const Http::ResponseHeaderMap&, MatchStatusVector&) const override {}
  void onHttpResponseTrailers(const Http::ResponseTrailerMap&, MatchStatusVector&) const override {}
};

/**
 * Any matcher (always matches).
 */
class AnyMatcher : public SimpleMatcher {
public:
  using SimpleMatcher::SimpleMatcher;

  // Extensions::Common::Tap::Matcher
  void onNewStream(MatchStatusVector& statuses) const override {
    statuses[my_index_].matches_ = true;
    statuses[my_index_].might_change_status_ = false;
  }
};

/**
 * Base class for the various HTTP header matchers.
 */
class HttpHeaderMatcherBase : public SimpleMatcher {
public:
  HttpHeaderMatcherBase(const envoy::config::tap::v3::HttpHeadersMatch& config,
                        const std::vector<MatcherPtr>& matchers);

protected:
  void matchHeaders(const Http::HeaderMap& headers, MatchStatusVector& statuses) const;

  const std::vector<Http::HeaderUtility::HeaderDataPtr> headers_to_match_;
};

/**
 * HTTP request headers matcher.
 */
class HttpRequestHeadersMatcher : public HttpHeaderMatcherBase {
public:
  using HttpHeaderMatcherBase::HttpHeaderMatcherBase;

  // Extensions::Common::Tap::Matcher
  void onHttpRequestHeaders(const Http::RequestHeaderMap& request_headers,
                            MatchStatusVector& statuses) const override {
    matchHeaders(request_headers, statuses);
  }
};

/**
 * HTTP request trailers matcher.
 */
class HttpRequestTrailersMatcher : public HttpHeaderMatcherBase {
public:
  using HttpHeaderMatcherBase::HttpHeaderMatcherBase;

  // Extensions::Common::Tap::Matcher
  void onHttpRequestTrailers(const Http::RequestTrailerMap& request_trailers,
                             MatchStatusVector& statuses) const override {
    matchHeaders(request_trailers, statuses);
  }
};

/**
 * HTTP response headers matcher.
 */
class HttpResponseHeadersMatcher : public HttpHeaderMatcherBase {
public:
  using HttpHeaderMatcherBase::HttpHeaderMatcherBase;

  // Extensions::Common::Tap::Matcher
  void onHttpResponseHeaders(const Http::ResponseHeaderMap& response_headers,
                             MatchStatusVector& statuses) const override {
    matchHeaders(response_headers, statuses);
  }
};

/**
 * HTTP response trailers matcher.
 */
class HttpResponseTrailersMatcher : public HttpHeaderMatcherBase {
public:
  using HttpHeaderMatcherBase::HttpHeaderMatcherBase;

  // Extensions::Common::Tap::Matcher
  void onHttpResponseTrailers(const Http::ResponseTrailerMap& response_trailers,
                              MatchStatusVector& statuses) const override {
    matchHeaders(response_trailers, statuses);
  }
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
