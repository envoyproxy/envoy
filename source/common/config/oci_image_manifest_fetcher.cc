#include "source/common/config/oci_image_manifest_fetcher.h"

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"

#include "remote_data_fetcher.h"

namespace Envoy {
namespace Config {
namespace DataFetcher {

OciImageManifestFetcher::OciImageManifestFetcher(Upstream::ClusterManager& cm,
                                                 const envoy::config::core::v3::HttpUri& uri,
                                                 const std::string& content_hash,
                                                 RemoteDataFetcherCallback& callback,
                                                 const std::string& authz_header_value)
    : RemoteDataFetcher(cm, uri, content_hash, callback), authz_header_value_(authz_header_value) {}

void OciImageManifestFetcher::fetch() {
  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(uri_);
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
  message->headers().setAuthorization(authz_header_value_);

  // TODO: set "Accept: application/vnd.oci.image.manifest.v1+json"
  ENVOY_LOG(info, "fetch oci image from [uri = {}]: start", uri_.uri());
  const auto thread_local_cluster = cm_.getThreadLocalCluster(uri_.cluster());
  if (thread_local_cluster != nullptr) {
    request_ = thread_local_cluster->httpAsyncClient().send(
        std::move(message), *this,
        Http::AsyncClient::RequestOptions().setTimeout(
            std::chrono::milliseconds(DurationUtil::durationToMilliseconds(uri_.timeout()))));
  } else {
    ENVOY_LOG(info, "fetch oci image [uri = {}]: no cluster {}", uri_.uri(), uri_.cluster());
    callback_.onFailure(FailureReason::Network);
  }
}

void OciImageManifestFetcher::onSuccess(const Http::AsyncClient::Request&,
                                        Http::ResponseMessagePtr&& response) {
  const uint64_t status_code = Http::Utility::getResponseStatus(response->headers());
  if (status_code == enumToInt(Http::Code::OK)) {
    ENVOY_LOG(info, "fetch oci image [uri = {}, body = {}]: success", uri_.uri(),
              response->body().toString());
    if (response->body().length() > 0) {
      auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
      const auto content_hash = Hex::encode(crypto_util.getSha256Digest(response->body()));

      // TODO(jewertow)
      // if (content_hash_ != content_hash) {
      //   ENVOY_LOG(info, "fetch oci image [uri = {}]: data is invalid", uri_.uri());
      //   callback_.onFailure(FailureReason::InvalidData);
      // } else {
      std::string body = response->bodyAsString();
      // Parse manifests response
      std::string digest_value;
      if (!body.empty()) {
        TRY_ASSERT_MAIN_THREAD {
          Json::ObjectSharedPtr json_body =
              THROW_OR_RETURN_VALUE(Json::Factory::loadFromString(body), Json::ObjectSharedPtr);
          auto layers = THROW_OR_RETURN_VALUE(json_body->getObjectArray("layers"),
                                              std::vector<Json::ObjectSharedPtr>);
          auto digest = layers[0]->getString("digest", "");
          if (digest->empty()) {
            ENVOY_LOG(error, "fetch oci image [uri = {}, body = {}]: could not parse digest",
                      uri_.uri(), response->body().toString());
          } else {
            ENVOY_LOG(info, "fetch oci image [uri = {}, digest = {}]: found digest", uri_.uri(),
                      digest->c_str());
            callback_.onSuccess(digest.value());
          }
        }
        END_TRY
        catch (EnvoyException& e) {
          ENVOY_LOG(error,
                    "fetch oci image [uri = {}, body = {}]: failed to parse response body to JSON",
                    uri_.uri(), response->body().toString());
        }
      }
    } else {
      ENVOY_LOG(info, "fetch oci image [uri = {}]: body is empty", uri_.uri());
      callback_.onFailure(FailureReason::Network);
    }
  } else {
    ENVOY_LOG(info, "fetch oci image [uri = {}, body = {}]: response status code {}", uri_.uri(),
              response->body().toString(), status_code);
    callback_.onFailure(FailureReason::Network);
  }

  request_ = nullptr;
}

} // namespace DataFetcher
} // namespace Config
} // namespace Envoy
