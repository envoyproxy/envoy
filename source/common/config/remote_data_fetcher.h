#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Config {
namespace DataFetcher {

/**
 * Failure reason.
 */
enum class FailureReason {
  /* A network error occurred causing remote data retrieval failure. */
  Network,
  /* A failure occurred when trying to verify remote data using sha256. */
  InvalidData,
};

/**
 * Callback used by remote data fetcher.
 */
class RemoteDataFetcherCallback {
public:
  virtual ~RemoteDataFetcherCallback() = default;

  /**
   * This function will be called when data is fetched successfully from remote.
   * @param data remote data
   */
  virtual void onSuccess(const std::string& data) PURE;

  /**
   * This function is called when error happens during fetching data.
   * @param reason failure reason.
   */
  virtual void onFailure(FailureReason reason) PURE;
};

/**
 * Remote data fetcher.
 */
class RemoteDataFetcher : public Logger::Loggable<Logger::Id::config>,
                          public Http::AsyncClient::Callbacks {
public:
  RemoteDataFetcher(Upstream::ClusterManager& cm, const envoy::config::core::v3::HttpUri& uri,
                    const std::string& content_hash, RemoteDataFetcherCallback& callback);

  ~RemoteDataFetcher() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request&,
                 Http::AsyncClient::FailureReason reason) override;

  /**
   * Fetch data from remote.
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
  const std::string content_hash_;
  RemoteDataFetcherCallback& callback_;

  Http::AsyncClient::Request* request_{};
};

using RemoteDataFetcherPtr = std::unique_ptr<RemoteDataFetcher>;

} // namespace DataFetcher
} // namespace Config
} // namespace Envoy
