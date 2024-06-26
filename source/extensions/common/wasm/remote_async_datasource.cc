#include "source/extensions/common/wasm/remote_async_datasource.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/utility.h"

#include "fmt/format.h"

namespace Envoy {

// Default Parameters of the jittered backoff strategy.
static constexpr uint32_t RetryInitialDelayMilliseconds = 1000;
static constexpr uint32_t RetryMaxDelayMilliseconds = 10 * 1000;
static constexpr uint32_t RetryCount = 1;

absl::optional<std::string> getPath(const envoy::config::core::v3::DataSource& source) {
  return source.specifier_case() == envoy::config::core::v3::DataSource::SpecifierCase::kFilename
             ? absl::make_optional(source.filename())
             : absl::nullopt;
}

RemoteAsyncDataProvider::RemoteAsyncDataProvider(
    Upstream::ClusterManager& cm, Init::Manager& manager,
    const envoy::config::core::v3::RemoteDataSource& source, Event::Dispatcher& dispatcher,
    Random::RandomGenerator& random, bool allow_empty, AsyncDataSourceCb&& callback)
    : allow_empty_(allow_empty), callback_(std::move(callback)),
      fetcher_(std::make_unique<Config::DataFetcher::RemoteDataFetcher>(cm, source.http_uri(),
                                                                        source.sha256(), *this)),
      init_target_("RemoteAsyncDataProvider", [this]() { start(); }),
      retries_remaining_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(source.retry_policy(), num_retries, RetryCount)) {

  auto strategy_or_error = Config::Utility::prepareJitteredExponentialBackOffStrategy(
      source, random, RetryInitialDelayMilliseconds, RetryMaxDelayMilliseconds);
  THROW_IF_STATUS_NOT_OK(strategy_or_error, throw);
  backoff_strategy_ = std::move(strategy_or_error.value());

  retry_timer_ = dispatcher.createTimer([this]() -> void { start(); });

  manager.add(init_target_);
}

} // namespace Envoy
