#pragma once

#include <string>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/secret/secret_provider_impl.h"
#include "source/extensions/common/wasm/oci/oci_image_blob_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

/**
 * OCI image manifest fetcher.
 */
class ImageManifestFetcher : public Config::DataFetcher::RemoteDataFetcher {
public:
  ImageManifestFetcher(
      Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
      Config::DataFetcher::RemoteDataFetcherCallback& callback,
      std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> image_pull_secret_provider,
      const std::string& registry);

  // Config::DataFetcher::RemoteDataFetcher
  void fetch() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;

private:
  void onInvalidData(std::string error_message);

  std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> image_pull_secret_provider_;
  const std::string registry_;

  Http::AsyncClient::Request* request_{};
  ImageBlobFetcherPtr blob_fetcher_;
};

using ImageManifestFetcherPtr = std::unique_ptr<ImageManifestFetcher>;

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
