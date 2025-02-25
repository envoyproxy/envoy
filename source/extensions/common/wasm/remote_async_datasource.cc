#include "source/extensions/common/wasm/remote_async_datasource.h"

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/utility.h"

#include "fmt/format.h"

namespace Envoy {

// Default Parameters of the jittered backoff strategy.
static constexpr uint32_t RetryInitialDelayMilliseconds = 1000;
static constexpr uint32_t RetryMaxDelayMilliseconds = 10 * 1000;
static constexpr uint32_t RetryCount = 1;

RemoteAsyncDataProvider::RemoteAsyncDataProvider(
    Upstream::ClusterManager& cm, Init::Manager& manager,
    const envoy::config::core::v3::RemoteDataSource& source, Event::Dispatcher& dispatcher,
    Random::RandomGenerator& random, bool allow_empty, AsyncDataSourceCb&& callback)
    : RemoteAsyncDataProvider(
          [this, &cm, &source]() {
            return std::make_unique<Config::DataFetcher::RemoteDataFetcher>(cm, source.http_uri(),
                                                                            source.sha256(), *this);
          },
          "RemoteAsyncDataProvider", manager, source, dispatcher, random, allow_empty,
          std::move(callback)){};

RemoteAsyncDataProvider::RemoteAsyncDataProvider(
    CreateFetcherFn&& create_fetcher_fn, absl::string_view target_name, Init::Manager& manager,
    const envoy::config::core::v3::RemoteDataSource& source, Event::Dispatcher& dispatcher,
    Random::RandomGenerator& random, bool allow_empty, AsyncDataSourceCb&& callback)
    : allow_empty_(allow_empty), callback_(std::move(callback)), fetcher_(create_fetcher_fn()),
      init_target_(target_name, [this]() { fetcher_->fetch(); }),
      retries_remaining_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(source.retry_policy(), num_retries, RetryCount)) {

  auto strategy_or_error = Config::Utility::prepareJitteredExponentialBackOffStrategy(
      source, random, RetryInitialDelayMilliseconds, RetryMaxDelayMilliseconds);
  THROW_IF_NOT_OK_REF(strategy_or_error.status());
  backoff_strategy_ = std::move(strategy_or_error.value());

  retry_timer_ = dispatcher.createTimer([this]() -> void { fetcher_->fetch(); });

  manager.add(init_target_);
}

} // namespace Envoy
