#pragma once

#include <string>

#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/config/remote_data_fetcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Oci {

/**
 * OCI image blob fetcher.
 */
class OciImageBlobFetcher : public Config::DataFetcher::RemoteDataFetcher {
public:
  OciImageBlobFetcher(Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
                      const std::string& content_hash,
                      Config::DataFetcher::RemoteDataFetcherCallback& callback,
                      const std::string& authz_header_value, const std::string& digest);

  // Config::DataFetcher::RemoteDataFetcher
  void fetch() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;

private:
  const std::string authz_header_value_;
  const std::string digest_;

  Http::AsyncClient::Request* request_{};
};

using OciImageBlobFetcherPtr = std::unique_ptr<OciImageBlobFetcher>;

} // namespace Oci
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
