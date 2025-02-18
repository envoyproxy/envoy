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
class OciImageManifestFetcher : public RemoteDataFetcher {
public:
  OciImageManifestFetcher(Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
                          const std::string& content_hash, RemoteDataFetcherCallback& callback,
                          const std::string& authz_header_value);

  // RemoteDataFetcher
  void fetch() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;

private:
  const std::string authz_header_value_;

  Http::AsyncClient::Request* request_{};
  OciImageBlobFetcherPtr blob_fetcher_;
};

using OciImageManifestFetcherPtr = std::unique_ptr<OciImageManifestFetcher>;

} // namespace DataFetcher
} // namespace Config
} // namespace Envoy
