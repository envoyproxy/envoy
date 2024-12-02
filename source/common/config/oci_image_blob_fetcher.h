#pragma once

#include <string>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "source/common/config/remote_data_fetcher.h"

namespace Envoy {
namespace Config {
namespace DataFetcher {

/**
 * OCI image blob fetcher.
 */
class OciImageBlobFetcher : public Logger::Loggable<Logger::Id::config>,
                            public Http::AsyncClient::Callbacks {
public:
  OciImageBlobFetcher(Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
                      const std::string& authz_header_value, const std::string& digest,
                      const std::string& content_hash, RemoteDataFetcherCallback& callback);

  ~OciImageBlobFetcher() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason reason) override;
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

  /**
   * Fetch OCI image blob.
   * @param uri remote URI
   * @param content_hash for verifying data integrity
   * @param callback callback when fetch is done.
   */
  void fetch();

  /**
   * Cancel the fetch.
   */
  void cancel();

private:
  Upstream::ClusterManager& cm_;
  const envoy::config::core::v3::HttpUri uri_;
  const std::string authz_header_value_;
  const std::string digest_;
  const std::string content_hash_;
  RemoteDataFetcherCallback& callback_;

  Http::AsyncClient::Request* request_{};
};

using OciImageBlobFetcherPtr = std::unique_ptr<OciImageBlobFetcher>;

} // namespace DataFetcher
} // namespace Config
} // namespace Envoy
