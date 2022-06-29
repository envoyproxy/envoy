#include "source/extensions/filters/http/custom_response/response.h"

#include <string>

#include "envoy/api/api.h"

#include "envoy/http/async_client.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/http/utility.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/header_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class RemoteResponseClient : public Http::AsyncClient::Callbacks,
                             public Logger::Loggable<Logger::Id::init> {
public:
  RemoteResponseClient(const envoy::extensions::filters::http::custom_response::v3::CustomResponse::
                           Response::RemoteDataSource& config,
                       Server::Configuration::FactoryContext& context)
      : config_(config), context_(context) {}

  ~RemoteResponseClient() override { cancel(); }

  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}
  void fetchBody(RequestCallbacks& callbacks, Http::RequestMessagePtr&& request);
  void cancel();

  // Http::AsyncClient::Callbacks implemented by this class.
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason reason) override;

private:
  void onError();
  envoy::extensions::filters::http::custom_response::v3::CustomResponse::Response::RemoteDataSource
      config_;
  Server::Configuration::FactoryContext& context_;
  Http::AsyncClient::Request* active_request_{};
  RequestCallbacks* callbacks_{};
};

Response::Response(
    const envoy::extensions::filters::http::custom_response::v3::CustomResponse::Response& config,
    Server::Configuration::CommonFactoryContext& context)
    : name_(config.name()),
      formatter_(config.has_body_format()
                     ? Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
                           config.body_format(), context)
                     : nullptr),
      content_type_(config.has_body_format()
                        ? std::optional<std::string>(
                              (!config.body_format().content_type().empty()
                                   ? config.body_format().content_type()
                                   : (config.body_format().format_case() ==
                                              envoy::config::core::v3::SubstitutionFormatString::
                                                  FormatCase::kJsonFormat
                                          ? Http::Headers::get().ContentTypeValues.Json
                                          : Http::Headers::get().ContentTypeValues.Text)))
                        : std::nullopt) {
  if (config.has_status_code()) {
    status_code_ = static_cast<Http::Code>(config.status_code().value());
  }
  // local data
  if (config.has_local()) {
    // TODO: is true right here?
    local_body_.emplace(Config::DataSource::read(config.local(), true, context.api()));
  } else if (config.has_remote()) {
    remote_data_source_.emplace(config.remote());
  }

  header_parser_ = Envoy::Router::HeaderParser::configure(config.headers_to_add());
}

void Response::rewrite(const Http::RequestHeaderMap& request_headers,
                       Http::ResponseHeaderMap& response_headers,
                       StreamInfo::StreamInfo& stream_info, Http::Code& code, std::string& body) {

  if (local_body_.has_value()) {
    body = local_body_.value();
  }

  header_parser_->evaluateHeaders(response_headers, stream_info);

  if (status_code_.has_value() && code != status_code_.value()) {
    code = status_code_.value();
    response_headers.setStatus(std::to_string(enumToInt(code)));
    stream_info.setResponseCode(static_cast<uint32_t>(code));
  }

  if (formatter_) {
    formatter_->format(request_headers, response_headers,
                       // TODO: do we need the actual trailers here?
                       *Http::StaticEmptyHeaders::get().response_trailers, stream_info, body);
  }
}

void RemoteResponseClient::fetchBody(RequestCallbacks& callbacks,
                                     Http::RequestMessagePtr&& request) {
  // Cancel any active requests.
  cancel();
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  const std::string cluster = config_.http_uri().cluster();
  const std::string uri = config_.http_uri().uri();
  const auto thread_local_cluster = context_.clusterManager().getThreadLocalCluster(cluster);

  // Failed to fetch the token if the cluster is not configured.
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "Failed to fetch the token: [cluster = {}] is not found or configured.",
              cluster);
    onError();
    return;
  }

  // Set up the request options.
  struct Envoy::Http::AsyncClient::RequestOptions options =
      Envoy::Http::AsyncClient::RequestOptions()
          .setTimeout(std::chrono::milliseconds(
              DurationUtil::durationToMilliseconds(config_.http_uri().timeout())))
          // GCP metadata server rejects X-Forwarded-For requests.
          // https://cloud.google.com/compute/docs/storing-retrieving-metadata#x-forwarded-for_header
          .setSendXff(false);

  if (config_.has_retry_policy()) {
    envoy::config::route::v3::RetryPolicy route_retry_policy =
        Http::Utility::convertCoreToRouteRetryPolicy(config_.retry_policy(),
                                                     "5xx,gateway-error,connect-failure,reset");
    options.setRetryPolicy(route_retry_policy);
    options.setBufferBodyForRetry(true);
  }

  active_request_ =
      thread_local_cluster->httpAsyncClient().send(std::move(request), *this, options);
}

void RemoteResponseClient::onSuccess(const Http::AsyncClient::Request&,
                                     Http::ResponseMessagePtr&& response) {
  auto status = Envoy::Http::Utility::getResponseStatusNoThrow(response->headers());
  active_request_ = nullptr;
  if (status.has_value()) {
    uint64_t status_code = status.value();
    if (status_code == Envoy::enumToInt(Envoy::Http::Code::OK)) {
      ASSERT(callbacks_ != nullptr);
      callbacks_->onComplete(response.get());
      callbacks_ = nullptr;
    } else {
      ENVOY_LOG(error, "Response status is not OK, status: {}", status_code);
      onError();
    }
  } else {
    // This occurs if the response headers are invalid.
    ENVOY_LOG(error, "Failed to get the response because response headers are not valid.");
    onError();
  }
}

void RemoteResponseClient::onFailure(const Http::AsyncClient::Request&,
                                     Http::AsyncClient::FailureReason reason) {
  // Http::AsyncClient::FailureReason only has one value: "Reset".
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset);
  ENVOY_LOG(error, "Request failed: stream has been reset");
  active_request_ = nullptr;
  onError();
}

void RemoteResponseClient::cancel() {
  if (active_request_) {
    active_request_->cancel();
    active_request_ = nullptr;
  }
}

void RemoteResponseClient::onError() {
  // Cancel if the request is active.
  cancel();

  ASSERT(callbacks_ != nullptr);
  callbacks_->onComplete(/*response_ptr=*/nullptr);
  callbacks_ = nullptr;
}

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
