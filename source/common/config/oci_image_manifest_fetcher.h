#pragma once

#include <string>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "source/common/config/oci_image_blob_fetcher.h"

namespace Envoy {
namespace Config {
namespace DataFetcher {

/**
 * OCI image manifest fetcher.
 */
class OciImageManifestFetcher : public Logger::Loggable<Logger::Id::config>,
                                public Http::AsyncClient::Callbacks {
public:
  OciImageManifestFetcher(Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
                          const std::string& token, const std::string& content_hash,
                          RemoteDataFetcherCallback& callback);

  ~OciImageManifestFetcher() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason reason) override;
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

  /**
   * Fetch OCI image manifest.
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
  const std::string content_hash_;
  RemoteDataFetcherCallback& callback_;

  Http::AsyncClient::Request* request_{};
  OciImageBlobFetcherPtr blob_fetcher_;
};

using OciImageManifestFetcherPtr = std::unique_ptr<OciImageManifestFetcher>;

} // namespace DataFetcher
} // namespace Config
} // namespace Envoy
