#pragma once

#include "envoy/config/filter/http/ext_authz/v2alpha/ext_authz.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/common/matchers.h"

#include "extensions/filters/common/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

class Matcher;
typedef std::shared_ptr<Matcher> MatcherSharedPtr;

/**
 *  Matchers describe the rules for matching authorization request and response headers.
 */
class Matcher {
public:
  virtual ~Matcher() {}

  /**
   * Returns whether or not the header key matches the rules of the matcher.
   *
   * @param key supplies the header key to be evaluated.
   */
  virtual bool matches(const Http::LowerCaseString& key) const PURE;

  /**
   * Returns whether or not the header key matches the rules of the matcher.
   *
   * @param key supplies the header key to be evaluated.
   */
  virtual bool matches(const Envoy::Http::HeaderString& key) const PURE;
};

class HeaderKeyMatcher : public Matcher {
public:
  HeaderKeyMatcher(std::vector<Matchers::StringMatcher>&& list) : matchers_(std::move(list)) {}

  bool matches(const Http::LowerCaseString& key) const override {
    return std::any_of(matchers_.begin(), matchers_.end(),
                       [&key](auto matcher) { return matcher.match(key.get()); });
  }

  bool matches(const Envoy::Http::HeaderString& key) const override {
    return std::any_of(matchers_.begin(), matchers_.end(),
                       [&key](auto matcher) { return matcher.match(key.getStringView()); });
  }

private:
  const std::vector<Matchers::StringMatcher> matchers_;
};

class NotHeaderKeyMatcher : public Matcher {
public:
  NotHeaderKeyMatcher(std::vector<Matchers::StringMatcher>&& list) : matcher_(std::move(list)) {}

  bool matches(const Http::LowerCaseString& key) const override { return !matcher_.matches(key); }

  bool matches(const Envoy::Http::HeaderString& key) const override {
    return !matcher_.matches(key);
  }

private:
  const HeaderKeyMatcher matcher_;
};

/**
 * HTTP client configuration for the HTTP authorization (ext_authz) filter.
 */
class ClientConfig {
public:
  ClientConfig(const envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz& config,
               uint32_t timeout, absl::string_view path_prefix);

  /**
   * @return Name of the authorization cluster.
   */
  const std::string& cluster() { return cluster_name_; }

  /**
   * @return Authorization request path prefix.
   */
  const std::string& pathPrefix() { return path_prefix_; }

  /**
   * @return Authorization request timeout.
   */
  const std::chrono::milliseconds& timeout() const { return timeout_; }

  /**
   * @return List of matchers used for selecting headers the should be aggregated to an
   * authorization request.
   */
  const MatcherSharedPtr& requestHeaderMatchers() const { return request_header_matchers_; }

  /**
   * @return List of matchers used for selecting headers the should be aggregated to an denied
   * authorization response.
   */
  const MatcherSharedPtr& clientHeaderMatchers() const { return client_header_matchers_; }

  /**
   * @return List of matchers used for selecting headers the should be aggregated to an ok
   * authorization response.
   */
  const MatcherSharedPtr& upstreamHeaderMatchers() const { return upstream_header_matchers_; }

  /**
   * @return List of headers that will be add to the authorization request.
   */
  const Http::LowerCaseStrPairVector& headersToAdd() const { return authorization_headers_to_add_; }

private:
  static MatcherSharedPtr toRequestMatchers(const envoy::type::matcher::ListStringMatcher& matcher);
  static MatcherSharedPtr toClientMatchers(const envoy::type::matcher::ListStringMatcher& matcher);
  static MatcherSharedPtr
  toUpstreamMatchers(const envoy::type::matcher::ListStringMatcher& matcher);
  static Http::LowerCaseStrPairVector
  toHeadersAdd(const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>&);

  const MatcherSharedPtr request_header_matchers_;
  const MatcherSharedPtr client_header_matchers_;
  const MatcherSharedPtr upstream_header_matchers_;
  const Http::LowerCaseStrPairVector authorization_headers_to_add_;
  const std::string cluster_name_;
  const std::chrono::milliseconds timeout_;
  const std::string path_prefix_;
};

typedef std::shared_ptr<ClientConfig> ClientConfigSharedPtr;

/**
 * This client implementation is used when the Ext_Authz filter needs to communicate with an
 * HTTP authorization server. Unlike the gRPC client that allows the server to define the
 * response object, in the HTTP client, all headers and body provided in the response are
 * dispatched to the downstream, and some headers to the upstream. The HTTP client also allows
 * setting a path prefix witch is not available for gRPC.
 */
class RawHttpClientImpl : public Client,
                          public Http::AsyncClient::Callbacks,
                          Logger::Loggable<Logger::Id::config> {
public:
  explicit RawHttpClientImpl(Upstream::ClusterManager& cm, ClientConfigSharedPtr config);
  ~RawHttpClientImpl();

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks,
             const envoy::service::auth::v2alpha::CheckRequest& request, Tracing::Span&) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& message) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

private:
  ResponsePtr toResponse(Http::MessagePtr message);
  Upstream::ClusterManager& cm_;
  ClientConfigSharedPtr config_;
  Http::AsyncClient::Request* request_{};
  RequestCallbacks* callbacks_{};
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
