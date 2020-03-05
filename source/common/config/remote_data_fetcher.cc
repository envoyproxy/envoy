#include "common/config/remote_data_fetcher.h"

#include "envoy/config/core/v3/http_uri.pb.h"

#include "common/common/enum_to_int.h"
#include "common/common/hex.h"
#include "common/crypto/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Config {
namespace DataFetcher {

RemoteDataFetcher::RemoteDataFetcher(Upstream::ClusterManager& cm,
                                     const envoy::config::core::v3::HttpUri& uri,
                                     const std::string& content_hash,
                                     RemoteDataFetcherCallback& callback)
    : cm_(cm), uri_(uri), content_hash_(content_hash), callback_(callback) {}

RemoteDataFetcher::~RemoteDataFetcher() { cancel(); }

void RemoteDataFetcher::cancel() {
  if (request_) {
    request_->cancel();
    ENVOY_LOG(debug, "fetch remote data [uri = {}]: canceled", uri_.uri());
  }

  request_ = nullptr;
}

void RemoteDataFetcher::fetch() {
  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(uri_);
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
  ENVOY_LOG(debug, "fetch remote data from [uri = {}]: start", uri_.uri());
  request_ = cm_.httpAsyncClientForCluster(uri_.cluster())
                 .send(std::move(message), *this,
                       Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(
                           DurationUtil::durationToMilliseconds(uri_.timeout()))));
}

void RemoteDataFetcher::onSuccess(Http::ResponseMessagePtr&& response) {
  const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code == enumToInt(Http::Code::OK)) {
    ENVOY_LOG(debug, "fetch remote data [uri = {}]: success", uri_.uri());
    if (response->body()) {
      auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
      const auto content_hash = Hex::encode(crypto_util.getSha256Digest(*response->body()));

      if (content_hash_ != content_hash) {
        ENVOY_LOG(debug, "fetch remote data [uri = {}]: data is invalid", uri_.uri());
        callback_.onFailure(FailureReason::InvalidData);
      } else {
        callback_.onSuccess(response->body()->toString());
      }
    } else {
      ENVOY_LOG(debug, "fetch remote data [uri = {}]: body is empty", uri_.uri());
      callback_.onFailure(FailureReason::Network);
    }
  } else {
    ENVOY_LOG(debug, "fetch remote data [uri = {}]: response status code {}", uri_.uri(),
              status_code);
    callback_.onFailure(FailureReason::Network);
  }

  request_ = nullptr;
}

void RemoteDataFetcher::onFailure(Http::AsyncClient::FailureReason reason) {
  ENVOY_LOG(debug, "fetch remote data [uri = {}]: network error {}", uri_.uri(), enumToInt(reason));
  request_ = nullptr;
  callback_.onFailure(FailureReason::Network);
}

} // namespace DataFetcher
} // namespace Config
} // namespace Envoy
