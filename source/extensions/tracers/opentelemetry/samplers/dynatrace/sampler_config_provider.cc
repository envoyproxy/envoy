#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.h"

#include <chrono>

#include "source/common/common/enum_to_int.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

static constexpr std::chrono::seconds INITIAL_TIMER_DURATION{10};
static constexpr std::chrono::minutes TIMER_INTERVAL{5};

namespace {

bool reEnableTimer(Http::Code response_code) {
  switch (response_code) {
  case Http::Code::OK:
  case Http::Code::TooManyRequests:
  case Http::Code::InternalServerError:
  case Http::Code::BadGateway:
  case Http::Code::ServiceUnavailable:
  case Http::Code::GatewayTimeout:
    return true;
  default:
    return false;
  }
}

} // namespace

SamplerConfigProviderImpl::SamplerConfigProviderImpl(
    Server::Configuration::TracerFactoryContext& context,
    const envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig& config)
    : cluster_manager_(context.serverFactoryContext().clusterManager()),
      http_uri_(config.http_service().http_uri()), sampler_config_(config.root_spans_per_minute()) {

  for (const auto& header_value_option : config.http_service().request_headers_to_add()) {
    parsed_headers_to_add_.push_back({Http::LowerCaseString(header_value_option.header().key()),
                                      header_value_option.header().value()});
  }

  timer_ = context.serverFactoryContext().mainThreadDispatcher().createTimer([this]() -> void {
    const auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(http_uri_.cluster());
    if (thread_local_cluster == nullptr) {
      ENVOY_LOG(error, "SamplerConfigProviderImpl failed: [cluster = {}] is not configured",
                http_uri_.cluster());
    } else {
      Http::RequestMessagePtr message = Http::Utility::prepareHeaders(http_uri_);
      message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Get);
      for (const auto& header_pair : parsed_headers_to_add_) {
        message->headers().setReference(header_pair.first, header_pair.second);
      }
      active_request_ = thread_local_cluster->httpAsyncClient().send(
          std::move(message), *this,
          Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(
              DurationUtil::durationToMilliseconds(http_uri_.timeout()))));
    }
  });

  timer_->enableTimer(std::chrono::seconds(INITIAL_TIMER_DURATION));
}

SamplerConfigProviderImpl::~SamplerConfigProviderImpl() {
  if (active_request_) {
    active_request_->cancel();
  }
}

void SamplerConfigProviderImpl::onSuccess(const Http::AsyncClient::Request& /*request*/,
                                          Http::ResponseMessagePtr&& http_response) {
  active_request_ = nullptr;
  const auto response_code = Http::Utility::getResponseStatus(http_response->headers());
  bool json_valid = false;
  if (response_code == enumToInt(Http::Code::OK)) {
    ENVOY_LOG(debug, "Received sampling configuration from Dynatrace: {}",
              http_response->bodyAsString());
    json_valid = sampler_config_.parse(http_response->bodyAsString());
    if (!json_valid) {
      ENVOY_LOG(warn, "Failed to parse sampling configuration received from Dynatrace: {}",
                http_response->bodyAsString());
    }
  } else {
    ENVOY_LOG(warn, "Failed to get sampling configuration from Dynatrace: {}", response_code);
  }

  if (json_valid || reEnableTimer(static_cast<Http::Code>(response_code))) {
    timer_->enableTimer(std::chrono::seconds(TIMER_INTERVAL));
  } else {
    ENVOY_LOG(error, "Stopped to query sampling configuration from Dynatrace.");
  }
}

void SamplerConfigProviderImpl::onFailure(const Http::AsyncClient::Request& /*request*/,
                                          Http::AsyncClient::FailureReason reason) {
  active_request_ = nullptr;
  timer_->enableTimer(std::chrono::seconds(TIMER_INTERVAL));
  ENVOY_LOG(warn, "Failed to get sampling configuration from Dynatrace. Reason {}",
            enumToInt(reason));
}

const SamplerConfig& SamplerConfigProviderImpl::getSamplerConfig() const { return sampler_config_; }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
