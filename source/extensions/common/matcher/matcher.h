#pragma once

#include "envoy/config/common/matcher/v3/matcher.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

class Matcher;
using MatcherPtr = std::unique_ptr<Matcher>;

/**
 * Base class for context used by individual matchers.
 * The context may be required by matchers which are called multiple times
 * and need to carry state between the calls. For example body matchers may
 * store information how any bytes of the body have been already processed
 * or what what has been already found in the body and what has yet to be found.
 */
class MatcherCtx {
public:
  virtual ~MatcherCtx() = default;
};

/**
 * Base class for all matchers.
 *
 * A high level note on the design of matching which is different from other matching in Envoy
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
    std::unique_ptr<MatcherCtx> ctx_{}; // Context used by matchers to save interim context.
  };

  using MatchStatusVector = std::vector<MatchStatus>;

  /**
   * Base class constructor for a matcher.
   * @param matchers supplies the match tree vector being built.
   */
  Matcher(const std::vector<MatcherPtr>& matchers)
      // NOTE: This code assumes that the index for the matcher being constructed has already been
      // allocated, which is why my_index_ is set to size() - 1. See buildMatcher() in
      // matcher.cc.
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
   * Update match status given HTTP request body.
   * @param data supplies the request body.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual void onRequestBody(const Buffer::Instance& data, MatchStatusVector& statuses) PURE;

  /**
   * Update match status given HTTP response body.
   * @param data supplies the response body.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  virtual void onResponseBody(const Buffer::Instance& data, MatchStatusVector& statuses) PURE;

  /**
   * @return whether given currently available information, the matcher matches.
   * @param statuses supplies the per-stream-request match status vector which must be the same
   *                 size as the match tree vector (see above).
   */
  const MatchStatus& matchStatus(const MatchStatusVector& statuses) const {
    return statuses[my_index_];
  }

protected:
  const size_t my_index_;
};

/**
 * Factory method to build a matcher given a match config. Calling this function may end
 * up recursively building many matchers, which will all be added to the passed in vector
 * of matchers. See the comments in matcher.h for the general structure of how matchers work.
 */
void buildMatcher(const envoy::config::common::matcher::v3::MatchPredicate& match_config,
                  std::vector<MatcherPtr>& matchers,
                  Server::Configuration::CommonFactoryContext& context);

/**
 * Base class for logic matchers that need to forward update calls to child matchers.
 */
class LogicMatcherBase : public Matcher {
public:
  using Matcher::Matcher;

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
  void onRequestBody(const Buffer::Instance& data, MatchStatusVector& statuses) override {
    updateLocalStatus(statuses, [&data](Matcher& m, MatchStatusVector& statuses) {
      m.onRequestBody(data, statuses);
    });
  }
  void onResponseBody(const Buffer::Instance& data, MatchStatusVector& statuses) override {
    updateLocalStatus(statuses, [&data](Matcher& m, MatchStatusVector& statuses) {
      m.onResponseBody(data, statuses);
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

  SetLogicMatcher(const envoy::config::common::matcher::v3::MatchPredicate::MatchSet& configs,
                  std::vector<MatcherPtr>& matchers, Type type,
                  Server::Configuration::CommonFactoryContext& context);

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
  NotMatcher(const envoy::config::common::matcher::v3::MatchPredicate& config,
             std::vector<MatcherPtr>& matchers,
             Server::Configuration::CommonFactoryContext& context);

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

  void onNewStream(MatchStatusVector&) const override {}
  void onHttpRequestHeaders(const Http::RequestHeaderMap&, MatchStatusVector&) const override {}
  void onHttpRequestTrailers(const Http::RequestTrailerMap&, MatchStatusVector&) const override {}
  void onHttpResponseHeaders(const Http::ResponseHeaderMap&, MatchStatusVector&) const override {}
  void onHttpResponseTrailers(const Http::ResponseTrailerMap&, MatchStatusVector&) const override {}
  void onRequestBody(const Buffer::Instance&, MatchStatusVector&) override {}
  void onResponseBody(const Buffer::Instance&, MatchStatusVector&) override {}
};

/**
 * Any matcher (always matches).
 */
class AnyMatcher : public SimpleMatcher {
public:
  using SimpleMatcher::SimpleMatcher;

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
  HttpHeaderMatcherBase(const envoy::config::common::matcher::v3::HttpHeadersMatch& config,
                        const std::vector<MatcherPtr>& matchers,
                        Server::Configuration::CommonFactoryContext& context);

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

  void onHttpResponseTrailers(const Http::ResponseTrailerMap& response_trailers,
                              MatchStatusVector& statuses) const override {
    matchHeaders(response_trailers, statuses);
  }
};

/**
 * Base class for body matchers.
 */
class HttpBodyMatcherBase : public SimpleMatcher {
public:
  HttpBodyMatcherBase(const std::vector<MatcherPtr>& matchers) : SimpleMatcher(matchers) {}

protected:
  // Limit search to specified number of bytes.
  // Value equal to zero means no limit.
  uint32_t limit_{};
};

/**
 * Context is used by HttpGenericBodyMatcher to:
 * - track how many bytes has been processed
 * - track patterns which have been found
 * - store last several seen bytes of the HTTP body (when pattern starts at the end of previous body
 *   chunk and continues at the beginning of the next body chunk)
 */
class HttpGenericBodyMatcherCtx : public MatcherCtx {
public:
  HttpGenericBodyMatcherCtx(const std::shared_ptr<std::vector<std::string>>& patterns,
                            size_t overlap_size)
      : patterns_(patterns) {
    // Initialize overlap_ buffer's capacity to fit the longest pattern - 1.
    // The length of the longest pattern is known and passed here as overlap_size.
    patterns_index_.resize(patterns_->size());
    std::iota(patterns_index_.begin(), patterns_index_.end(), 0);
    overlap_.reserve(overlap_size);
    capacity_ = overlap_size;
  }
  ~HttpGenericBodyMatcherCtx() override = default;

  // The context is initialized per each http request. The patterns_
  // shared pointer attaches to matcher's list of patterns, so patterns
  // can be referenced without copying data.
  const std::shared_ptr<const std::vector<std::string>> patterns_;
  // List stores indexes of patterns in patterns_ shared memory which
  // still need to be located in the body. When a pattern is found
  // its index is removed from the list.
  // When all patterns have been found, the list is empty.
  std::list<uint32_t> patterns_index_;
  // Buffer to store the last bytes from previous body chunk(s).
  // It will store only as many bytes as is the length of the longest
  // pattern to be found minus 1.
  // It is necessary to locate patterns which are spread across 2 or more
  // body chunks.
  std::vector<char> overlap_;
  // capacity_ tells how many bytes should be buffered. overlap_'s initial
  // capacity is set to the length of the longest pattern - 1. As patterns
  // are found, there is a possibility that not as many bytes are required to be buffered.
  // It must be tracked outside of vector, because vector::reserve does not
  // change capacity when new value is lower than current capacity.
  uint32_t capacity_{};
  // processed_bytes_ tracks how many bytes of HTTP body have been processed.
  uint32_t processed_bytes_{};
};

class HttpGenericBodyMatcher : public HttpBodyMatcherBase {
public:
  HttpGenericBodyMatcher(const envoy::config::common::matcher::v3::HttpGenericBodyMatch& config,
                         const std::vector<MatcherPtr>& matchers);

protected:
  void onBody(const Buffer::Instance&, MatchStatusVector&);
  void onNewStream(MatchStatusVector& statuses) const override {
    // Allocate a new context used for the new stream.
    statuses[my_index_].ctx_ =
        std::make_unique<HttpGenericBodyMatcherCtx>(patterns_, overlap_size_);
    statuses[my_index_].matches_ = false;
    statuses[my_index_].might_change_status_ = true;
  }
  bool locatePatternAcrossChunks(const std::string& pattern, const Buffer::Instance& data,
                                 const HttpGenericBodyMatcherCtx* ctx);
  void bufferLastBytes(const Buffer::Instance& data, HttpGenericBodyMatcherCtx* ctx);

  size_t calcLongestPatternSize(const std::list<uint32_t>& indexes) const;
  void resizeOverlapBuffer(HttpGenericBodyMatcherCtx* ctx);

private:
  // The following fields are initialized based on matcher config and are used
  // by all HTTP matchers.
  // List of strings which body must contain to get match.
  std::shared_ptr<std::vector<std::string>> patterns_;
  // Stores the length of the longest pattern.
  size_t overlap_size_{};
};

class HttpRequestGenericBodyMatcher : public HttpGenericBodyMatcher {
public:
  using HttpGenericBodyMatcher::HttpGenericBodyMatcher;

  void onRequestBody(const Buffer::Instance& data, MatchStatusVector& statuses) override {
    onBody(data, statuses);
  }
};

class HttpResponseGenericBodyMatcher : public HttpGenericBodyMatcher {
public:
  using HttpGenericBodyMatcher::HttpGenericBodyMatcher;

  void onResponseBody(const Buffer::Instance& data, MatchStatusVector& statuses) override {
    onBody(data, statuses);
  }
};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
