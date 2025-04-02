#pragma once

#include <string>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/config/remote_data_fetcher.h"
#include "source/common/secret/secret_provider_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

/**
 * OCI image blob fetcher.
 */
class ImageBlobFetcher : public Config::DataFetcher::RemoteDataFetcher {
public:
  ImageBlobFetcher(
      Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
      const std::string& content_hash, Config::DataFetcher::RemoteDataFetcherCallback& callback,
      std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> image_pull_secret_provider,
      const std::string& registry);

  // Config::DataFetcher::RemoteDataFetcher
  void fetch() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;

private:
  std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> image_pull_secret_provider_;
  const std::string registry_;

  Http::AsyncClient::Request* request_{};
};

using ImageBlobFetcherPtr = std::unique_ptr<ImageBlobFetcher>;

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
