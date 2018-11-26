#pragma once

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
  explicit RawHttpClientImpl(Upstream::ClusterManager& cluster_manager,
                             const std::string& cluster_name, const std::string& path_prefix,
                             const absl::optional<std::chrono::milliseconds>& timeout,
                             const std::vector<Matchers::StringMatcher>& allowed_request_headers,
                             const std::vector<Matchers::StringMatcher>& allowed_client_headers,
                             const std::vector<Matchers::StringMatcher>& allowed_upstream_headers,
                             const Http::LowerCaseStrPairVector& authorization_headers_to_add);
  ~RawHttpClientImpl();

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks,
             const envoy::service::auth::v2alpha::CheckRequest& request, Tracing::Span&) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& message) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

private:
  ResponsePtr messageToResponse(Http::MessagePtr message);
  Upstream::ClusterManager& cm_;
  const std::string& cluster_name_;
  const std::string& path_prefix_;
  const absl::optional<std::chrono::milliseconds>& timeout_;
  const std::vector<Matchers::StringMatcher> allowed_request_headers_;
  const std::vector<Matchers::StringMatcher> allowed_client_headers_;
  const std::vector<Matchers::StringMatcher> allowed_upstream_headers_;
  const Http::LowerCaseStrPairVector authorization_headers_to_add_;

  Http::AsyncClient::Request* request_{};
  RequestCallbacks* callbacks_{};
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
