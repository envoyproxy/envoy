#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/common/matchers.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/common/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

class Matcher;
using MatcherSharedPtr = std::shared_ptr<Matcher>;

/**
 *  Matchers describe the rules for matching authorization request and response headers.
 */
class Matcher {
public:
  virtual ~Matcher() = default;

  /**
   * Returns whether or not the header key matches the rules of the matcher.
   *
   * @param key supplies the header key to be evaluated.
   */
  virtual bool matches(absl::string_view key) const PURE;
};

class HeaderKeyMatcher : public Matcher {
public:
  HeaderKeyMatcher(std::vector<Matchers::StringMatcherPtr>&& list);

  bool matches(absl::string_view key) const override;

private:
  const std::vector<Matchers::StringMatcherPtr> matchers_;
};

class NotHeaderKeyMatcher : public Matcher {
public:
  NotHeaderKeyMatcher(std::vector<Matchers::StringMatcherPtr>&& list);

  bool matches(absl::string_view key) const override;

private:
  const HeaderKeyMatcher matcher_;
};

/**
 * HTTP client configuration for the HTTP authorization (ext_authz) filter.
 */
class ClientConfig {
public:
  ClientConfig(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& config,
               uint32_t timeout, absl::string_view path_prefix);

  /**
   * Returns the name of the authorization cluster.
   */
  const std::string& cluster() { return cluster_name_; }

  /**
   * Returns the authorization request path prefix.
   */
  const std::string& pathPrefix() { return path_prefix_; }

  /**
   * Returns authorization request timeout.
   */
  const std::chrono::milliseconds& timeout() const { return timeout_; }

  /**
   * Returns a list of matchers used for selecting the request headers that should be sent to the
   * authorization server.
   */
  const MatcherSharedPtr& requestHeaderMatchers() const { return request_header_matchers_; }

  /**
   * Returns a list of matchers used for selecting the authorization response headers that
   * should be send back to the client.
   */
  const MatcherSharedPtr& clientHeaderMatchers() const { return client_header_matchers_; }

  /**
   * Returns a list of matchers used for selecting the authorization response headers that
   * should be send to an the upstream server.
   */
  const MatcherSharedPtr& upstreamHeaderMatchers() const { return upstream_header_matchers_; }

  /**
   * Returns a list of headers that will be add to the authorization request.
   */
  const Http::LowerCaseStrPairVector& headersToAdd() const { return authorization_headers_to_add_; }

  /**
   * Returns the name used for tracing.
   */
  const std::string& tracingName() { return tracing_name_; }

private:
  static MatcherSharedPtr
  toRequestMatchers(const envoy::type::matcher::v3::ListStringMatcher& matcher,
                    bool enable_case_sensitive_string_matcher);
  static MatcherSharedPtr
  toClientMatchers(const envoy::type::matcher::v3::ListStringMatcher& matcher,
                   bool enable_case_sensitive_string_matcher);
  static MatcherSharedPtr
  toUpstreamMatchers(const envoy::type::matcher::v3::ListStringMatcher& matcher,
                     bool enable_case_sensitive_string_matcher);
  static Http::LowerCaseStrPairVector
  toHeadersAdd(const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>&);

  const bool enable_case_sensitive_string_matcher_;
  const MatcherSharedPtr request_header_matchers_;
  const MatcherSharedPtr client_header_matchers_;
  const MatcherSharedPtr upstream_header_matchers_;
  const Http::LowerCaseStrPairVector authorization_headers_to_add_;
  const std::string cluster_name_;
  const std::chrono::milliseconds timeout_;
  const std::string path_prefix_;
  const std::string tracing_name_;
};

using ClientConfigSharedPtr = std::shared_ptr<ClientConfig>;

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
  explicit RawHttpClientImpl(Upstream::ClusterManager& cm, ClientConfigSharedPtr config,
                             TimeSource& time_source);
  ~RawHttpClientImpl() override;

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks, const envoy::service::auth::v3::CheckRequest& request,
             Tracing::Span&) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::ResponseMessagePtr&& message) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

private:
  ResponsePtr toResponse(Http::ResponseMessagePtr message);
  Upstream::ClusterManager& cm_;
  ClientConfigSharedPtr config_;
  Http::AsyncClient::Request* request_{};
  RequestCallbacks* callbacks_{};
  TimeSource& time_source_;
  Tracing::SpanPtr span_;
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
