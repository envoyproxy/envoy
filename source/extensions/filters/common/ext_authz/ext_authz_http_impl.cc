#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "common/common/enum_to_int.h"
#include "common/http/async_client_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

namespace {

const Http::HeaderMap* getZeroContentLengthHeader() {
  static const Http::HeaderMap* header_map =
      new Http::HeaderMapImpl{{Http::Headers::get().ContentLength, std::to_string(0)}};
  return header_map;
}
} // namespace

RawHttpClientImpl::RawHttpClientImpl(
    const std::string& cluster_name, Upstream::ClusterManager& cluster_manager,
    const absl::optional<std::chrono::milliseconds>& timeout, const std::string& path_prefix,
    const std::vector<Http::LowerCaseString>& response_headers_to_remove)
    : cluster_name_(cluster_name), path_prefix_(path_prefix),
      response_headers_to_remove_(response_headers_to_remove), timeout_(timeout),
      cm_(cluster_manager) {}

RawHttpClientImpl::~RawHttpClientImpl() { ASSERT(!callbacks_); }

void RawHttpClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void RawHttpClientImpl::check(RequestCallbacks& callbacks,
                              const envoy::service::auth::v2alpha::CheckRequest& request,
                              Tracing::Span&) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  Http::HeaderMapPtr headers = std::make_unique<Http::HeaderMapImpl>(*getZeroContentLengthHeader());
  for (const auto& header : request.attributes().request().http().headers()) {

    const Http::LowerCaseString key{header.first};
    if (key == Http::Headers::get().Path && !path_prefix_.empty()) {
      std::string value;
      absl::StrAppend(&value, path_prefix_, header.second);
      headers->addCopy(key, value);
    } else {
      headers->addCopy(key, header.second);
    }
  }

  request_ = cm_.httpAsyncClientForCluster(cluster_name_)
                 .send(std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers)), *this,
                       timeout_);
}

void RawHttpClientImpl::onSuccess(Http::MessagePtr&& response) {
  ResponsePtr authz_response = std::make_unique<Response>(Response{});

  uint64_t status_code;
  if (StringUtil::atoul(response->headers().Status()->value().c_str(), status_code)) {
    if (status_code == enumToInt(Http::Code::OK)) {
      // Header that should not be sent to the upstream.
      response->headers().removeStatus();
      response->headers().removeMethod();
      response->headers().removePath();
      response->headers().removeContentLength();

      // Optional/Configurable headers the should not be sent to the upstream.
      for (const auto& header_to_remove : response_headers_to_remove_) {
        response->headers().remove(header_to_remove);
      }

      authz_response->status = CheckStatus::OK;
      authz_response->status_code = Http::Code::OK;
    } else {
      authz_response->status = CheckStatus::Denied;
      authz_response->body = response->bodyAsString();
      authz_response->status_code = static_cast<Http::Code>(status_code);
    }
  } else {
    ENVOY_LOG(warn, "Authz_Ext failed to parse the HTTP response code.");
    authz_response->status_code = Http::Code::Forbidden;
    authz_response->status = CheckStatus::Denied;
  }

  response->headers().iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        static_cast<Http::HeaderVector*>(context)->emplace_back(
            Http::LowerCaseString{header.key().c_str()}, std::string{header.value().c_str()});
        return Http::HeaderMap::Iterate::Continue;
      },
      &authz_response->headers_to_add);

  callbacks_->onComplete(std::move(authz_response));
  callbacks_ = nullptr;
}

void RawHttpClientImpl::onFailure(Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset);
  Response authz_response{};
  authz_response.status = CheckStatus::Error;
  callbacks_->onComplete(std::make_unique<Response>(authz_response));
  callbacks_ = nullptr;
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
