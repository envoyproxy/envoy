#pragma once

#include "extensions/filters/common/ext_authz/ext_authz.h"

#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

class RawHttpClientImpl : public Client, public Http::AsyncClient::Callbacks {
public:
  explicit RawHttpClientImpl(const std::string& cluster_name,
                             Upstream::ClusterManager& cluster_manager,
                             const absl::optional<std::chrono::milliseconds>& timeout,
                             const std::string& path_prefix);
  ~RawHttpClientImpl();

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks,
             const envoy::service::auth::v2alpha::CheckRequest& request, Tracing::Span&) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& response) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

private:
  const std::string cluster_name_;
  const std::string path_prefix_;
  absl::optional<std::chrono::milliseconds> timeout_;
  Upstream::ClusterManager& cm_;
  Http::AsyncClient::Request* request_{};
  RequestCallbacks* callbacks_{};
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
