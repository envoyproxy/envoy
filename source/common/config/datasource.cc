#include "common/config/datasource.h"

#include "envoy/api/v2/core/base.pb.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {
namespace DataSource {

// Parameters of the jittered backoff strategy.
static constexpr uint32_t RetryInitialDelayMilliseconds = 1000;
static constexpr uint32_t RetryMaxDelayMilliseconds = 10 * 1000;
static constexpr uint32_t RetryCount = 1;

std::string read(const envoy::api::v2::core::DataSource& source, bool allow_empty, Api::Api& api) {
  switch (source.specifier_case()) {
  case envoy::api::v2::core::DataSource::kFilename:
    return api.fileSystem().fileReadToEnd(source.filename());
  case envoy::api::v2::core::DataSource::kInlineBytes:
    return source.inline_bytes();
  case envoy::api::v2::core::DataSource::kInlineString:
    return source.inline_string();
  default:
    if (!allow_empty) {
      throw EnvoyException(
          fmt::format("Unexpected DataSource::specifier_case(): {}", source.specifier_case()));
    }
    return "";
  }
}

absl::optional<std::string> getPath(const envoy::api::v2::core::DataSource& source) {
  return source.specifier_case() == envoy::api::v2::core::DataSource::kFilename
             ? absl::make_optional(source.filename())
             : absl::nullopt;
}

RemoteAsyncDataProvider::RemoteAsyncDataProvider(
    Upstream::ClusterManager& cm, Init::Manager& manager,
    const envoy::api::v2::core::RemoteDataSource& source, Event::Dispatcher& dispatcher,
    Runtime::RandomGenerator& random, bool allow_empty, AsyncDataSourceCb&& callback)
    : allow_empty_(allow_empty), callback_(std::move(callback)),
      fetcher_(std::make_unique<Config::DataFetcher::RemoteDataFetcher>(cm, source.http_uri(),
                                                                        source.sha256(), *this)),
      init_target_("RemoteAsyncDataProvider", [this]() { start(); }) {
  manager.add(init_target_);

  uint64_t base_interval_ms = RetryInitialDelayMilliseconds;
  uint64_t max_interval_ms = RetryMaxDelayMilliseconds;
  if (source.has_retry_policy()) {
    if (source.retry_policy().has_retry_back_off()) {
      base_interval_ms =
          PROTOBUF_GET_MS_REQUIRED(source.retry_policy().retry_back_off(), base_interval);
      max_interval_ms = PROTOBUF_GET_MS_OR_DEFAULT(source.retry_policy().retry_back_off(),
                                                   max_interval, base_interval_ms * 10);
    }

    if (source.retry_policy().has_num_retries()) {
      retries_remaining_ =
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(source.retry_policy(), num_retries, RetryCount);
    }
  }

  backoff_strategy_ =
      std::make_unique<JitteredBackOffStrategy>(base_interval_ms, max_interval_ms, random);
  retry_timer_ = dispatcher.createTimer([this]() -> void { start(); });
}

} // namespace DataSource
} // namespace Config
} // namespace Envoy
