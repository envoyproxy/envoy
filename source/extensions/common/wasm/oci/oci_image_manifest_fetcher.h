#pragma once

#include <string>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/common/wasm/oci/oci_image_blob_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

/**
 * OCI image manifest fetcher.
 */
class OciImageManifestFetcher : public Config::DataFetcher::RemoteDataFetcher {
public:
  OciImageManifestFetcher(Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
                          const std::string& content_hash,
                          Config::DataFetcher::RemoteDataFetcherCallback& callback,
                          const std::string& authz_header_value);

  // Config::DataFetcher::RemoteDataFetcher
  void fetch() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;

private:
  void onInvalidData(std::string error_message);

  const std::string authz_header_value_;

  Http::AsyncClient::Request* request_{};
  OciImageBlobFetcherPtr blob_fetcher_;
};

using OciImageManifestFetcherPtr = std::unique_ptr<OciImageManifestFetcher>;

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
