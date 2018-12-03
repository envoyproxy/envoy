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
  const std::vector<Matchers::StringMatcher>& requestHeaderMatchers() const {
    return request_header_matchers_;
  }

  /**
   * @return List of matchers used for selecting headers the should be aggregated to an denied
   * authorization response.
   */
  const std::vector<Matchers::StringMatcher>& clientHeaderMatchers() const {
    return client_header_matchers_;
  }

  /**
   * @return List of matchers used for selecting headers the should be aggregated to an ok
   * authorization response.
   */
  const std::vector<Matchers::StringMatcher>& upstreamHeaderMatchers() const {
    return upstream_header_matchers_;
  }

  /**
   * @return List of headers that will be add to the authorization request.
   */
  const Http::LowerCaseStrPairVector& headersToAdd() const { return authorization_headers_to_add_; }

private:
  static std::vector<Matchers::StringMatcher>
  toRequestMatchers(const envoy::type::matcher::ListStringMatcher& matcher);
  static std::vector<Matchers::StringMatcher>
  toClientMatchers(const envoy::type::matcher::ListStringMatcher& matcher);
  static std::vector<Matchers::StringMatcher>
  toUpstreamMatchers(const envoy::type::matcher::ListStringMatcher& matcher);
  static Http::LowerCaseStrPairVector
  toHeadersAdd(const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>&);

  const std::vector<Matchers::StringMatcher> request_header_matchers_;
  const std::vector<Matchers::StringMatcher> client_header_matchers_;
  const std::vector<Matchers::StringMatcher> upstream_header_matchers_;
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
