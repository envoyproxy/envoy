#include "test/extensions/filters/http/ext_proc/ext_proc_test_server.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "envoy/grpc/async_client.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/common/ext_proc/grpc_client_impl.h"
#include "source/extensions/filters/http/ext_proc/config.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/integration/fake_upstream.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

enum class ProcessingEvent { InitialMetadata, Message, RemoteClose };

// Records callbacks after the production handler returns. Waiting on the event log leaves simulated
// time fixed while the Envoy worker or Google gRPC completion thread processes real network I/O.
class ProcessingEventObserver {
public:
  void complete(const std::string& stream_id, ProcessingEvent event,
                std::optional<ProcessingResponse> response = std::nullopt) {
    absl::MutexLock lock(&mutex_);
    completed_events_.push_back({stream_id, event, std::move(response)});
  }

  bool waitFor(const std::string& stream_id, ProcessingEvent event,
               const ProcessingResponse* response) {
    const auto matches = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
      return std::any_of(
          completed_events_.begin(), completed_events_.end(), [&](const RecordedEvent& recorded) {
            return recorded.stream_id == stream_id && recorded.event == event &&
                   (!response ||
                    (recorded.response && TestUtility::protoEqual(*recorded.response, *response)));
          });
    };
    absl::MutexLock lock(&mutex_);
    return mutex_.AwaitWithTimeout(absl::Condition(&matches),
                                   absl::FromChrono(TestUtility::DefaultTimeout));
  }

  void clear() {
    absl::MutexLock lock(&mutex_);
    completed_events_.clear();
  }

private:
  struct RecordedEvent {
    std::string stream_id;
    ProcessingEvent event;
    std::optional<ProcessingResponse> response;
  };

  absl::Mutex mutex_;
  std::vector<RecordedEvent> completed_events_ ABSL_GUARDED_BY(mutex_);
};

class ObservedGrpcCallbacks : public Grpc::RawAsyncStreamCallbacks {
public:
  ObservedGrpcCallbacks(Grpc::RawAsyncStreamCallbacks& callbacks,
                        std::shared_ptr<ProcessingEventObserver> observer)
      : callbacks_(callbacks), stream_id_(fmt::format("{:p}", static_cast<void*>(this))),
        observer_(std::move(observer)) {}

  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override {
    callbacks_.onCreateInitialMetadata(metadata);
    metadata.setCopy(Http::LowerCaseString("x-ext-proc-test-stream-id"), stream_id_);
  }
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override {
    auto observer = observer_;
    const auto stream_id = stream_id_;
    callbacks_.onReceiveInitialMetadata(std::move(metadata));
    observer->complete(stream_id, ProcessingEvent::InitialMetadata);
  }
  bool onReceiveMessageRaw(Buffer::InstancePtr&& message) override {
    auto observer = observer_;
    const auto stream_id = stream_id_;
    ProcessingResponse response;
    const bool parsed = response.ParseFromString(message->toString());
    const bool accepted = callbacks_.onReceiveMessageRaw(std::move(message));
    if (parsed && accepted) {
      observer->complete(stream_id, ProcessingEvent::Message, std::move(response));
    }
    return accepted;
  }
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override {
    callbacks_.onReceiveTrailingMetadata(std::move(metadata));
  }
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
    // The forwarded callback can delete this stream and its callback adapter.
    auto observer = observer_;
    const auto stream_id = stream_id_;
    callbacks_.onRemoteClose(status, message);
    observer->complete(stream_id, ProcessingEvent::RemoteClose);
  }

private:
  Grpc::RawAsyncStreamCallbacks& callbacks_;
  const std::string stream_id_;
  const std::shared_ptr<ProcessingEventObserver> observer_;
};

class ObservedGrpcClient : public Grpc::RawAsyncClient {
public:
  ObservedGrpcClient(Grpc::RawAsyncClientSharedPtr client,
                     std::shared_ptr<ProcessingEventObserver> observer)
      : client_(std::move(client)), observer_(std::move(observer)) {}

  Grpc::AsyncRequest* sendRaw(absl::string_view service, absl::string_view method,
                              Buffer::InstancePtr&& request,
                              Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span& span,
                              const Http::AsyncClient::RequestOptions& options) override {
    return client_->sendRaw(service, method, std::move(request), callbacks, span, options);
  }
  Grpc::RawAsyncStream* startRaw(absl::string_view service, absl::string_view method,
                                 Grpc::RawAsyncStreamCallbacks& callbacks,
                                 const Http::AsyncClient::StreamOptions& options) override {
    callbacks_.push_back(std::make_unique<ObservedGrpcCallbacks>(callbacks, observer_));
    return client_->startRaw(service, method, *callbacks_.back(), options);
  }
  absl::string_view destination() override { return client_->destination(); }

private:
  const Grpc::RawAsyncClientSharedPtr client_;
  const std::shared_ptr<ProcessingEventObserver> observer_;
  std::vector<std::unique_ptr<ObservedGrpcCallbacks>> callbacks_;
};

using ProcessorClientImpl =
    CommonExtProc::ProcessorClientImpl<ProcessingRequest, ProcessingResponse>;

class ObservedProcessorClient : public ProcessorClientImpl {
public:
  ObservedProcessorClient(Grpc::AsyncClientManager& manager, Stats::Scope& scope,
                          std::shared_ptr<ProcessingEventObserver> observer)
      : ProcessorClientImpl(manager, scope, "envoy.service.ext_proc.v3.ExternalProcessor.Process"),
        manager_(manager), scope_(scope), observer_(std::move(observer)) {}

  ExternalProcessorStreamPtr
  start(ExternalProcessorCallbacks& callbacks, const Grpc::GrpcServiceConfigWithHashKey& config,
        Http::AsyncClient::StreamOptions& options,
        Http::StreamFilterSidestreamWatermarkCallbacks& watermark_callbacks) override {
    auto client = manager_.getOrCreateRawAsyncClientWithHashKey(config, scope_, true);
    if (!client.ok()) {
      return ProcessorClientImpl::start(callbacks, config, options, watermark_callbacks);
    }
    auto observed = std::make_shared<ObservedGrpcClient>(*client, observer_);
    return CommonExtProc::ProcessorStreamImpl<ProcessingRequest, ProcessingResponse>::create(
        Grpc::AsyncClient<ProcessingRequest, ProcessingResponse>(observed), callbacks, options,
        watermark_callbacks, "envoy.service.ext_proc.v3.ExternalProcessor.Process");
  }

private:
  Grpc::AsyncClientManager& manager_;
  Stats::Scope& scope_;
  const std::shared_ptr<ProcessingEventObserver> observer_;
};

// Installs ObservedProcessorClient at the point where the production factory normally constructs
// ProcessorClientImpl. This lets the fake server wait until Envoy has applied a response before the
// test advances simulated time. The ext_proc protocol has no acknowledgement for responses such as
// timeout overrides, so sending bytes from FakeStream alone cannot provide that synchronization.
//
// FilterConfig, Filter, ProcessorStreamImpl, and the configured Envoy or Google gRPC client remain
// production implementations. Only the raw gRPC callbacks are wrapped to record their completion.
// Inherit the production factory's per-route configuration handling as well.
class ObservedFilterFactory : public ExternalProcessingFilterConfig {
public:
  explicit ObservedFilterFactory(std::shared_ptr<ProcessingEventObserver> observer)
      : observer_(std::move(observer)) {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override {
    absl::Status status;
    auto config = std::make_shared<FilterConfig>(
        proto_config,
        std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, message_timeout, 200)),
        PROTOBUF_GET_MS_OR_DEFAULT(proto_config, max_message_timeout, 0),
        extra_context.scopeOr(context), extra_context.stats_prefix, extra_context.is_upstream,
        Envoy::Extensions::Filters::Common::Expr::getBuilder(context), context, status);
    RETURN_IF_NOT_OK_REF(status);
    return [config, &context, observer = observer_](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamFilter(
          std::make_shared<Filter>(config, std::make_unique<ObservedProcessorClient>(
                                               context.clusterManager().grpcAsyncClientManager(),
                                               context.scope(), observer)));
    };
  }

  const std::shared_ptr<ProcessingEventObserver> observer_;
};

// Keeps the observing factory installed for one test fixture, for both downstream and upstream
// ext_proc filters. Registration is scoped so the original production factory is restored
// afterward.
//
// Each registry exposes the same factory under the canonical name "envoy.filters.http.ext_proc" and
// the legacy alias "envoy.ext_proc". InjectFactory replaces the canonical entry and rebuilds the
// protobuf-type lookup. Leaving the alias pointing at the original factory would register the same
// config type for two different factories. Swap the alias before injection, and restore it before
// InjectFactory's destructor rebuilds the lookup again.
class ObservedFilterFactoryRegistration {
  template <class Base> class Registration {
  public:
    explicit Registration(ObservedFilterFactory& factory)
        : original_alias_(Registry::FactoryRegistry<Base>::getFactory("envoy.ext_proc")) {
      Registry::FactoryRegistry<Base>::factories()["envoy.ext_proc"] = &factory;
      registration_ = std::make_unique<Registry::InjectFactory<Base>>(factory);
    }
    ~Registration() {
      Registry::FactoryRegistry<Base>::factories()["envoy.ext_proc"] = original_alias_;
      registration_.reset();
    }

  private:
    Base* original_alias_;
    std::unique_ptr<Registry::InjectFactory<Base>> registration_;
  };

public:
  explicit ObservedFilterFactoryRegistration(std::shared_ptr<ProcessingEventObserver> observer)
      : factory_(std::move(observer)), downstream_(factory_), upstream_(factory_) {}

private:
  ObservedFilterFactory factory_;
  Registration<Server::Configuration::NamedHttpFilterConfigFactory> downstream_;
  Registration<Server::Configuration::UpstreamHttpFilterConfigFactory> upstream_;
};

} // namespace

class ExtProcTestServer::Impl {
public:
  explicit Impl(TimeSource& time_source)
      : time_source_(time_source), events_(std::make_shared<ProcessingEventObserver>()),
        factory_registration_(events_) {}

  testing::AssertionResult waitForEvent(FakeStream& stream, ProcessingEvent event,
                                        const std::function<void()>& action,
                                        const ProcessingResponse* response = nullptr) {
    // Correlate the callback with this exact stream, including per-route service overrides.
    const auto ids = stream.headers().get(Http::LowerCaseString("x-ext-proc-test-stream-id"));
    if (ids.size() != 1) {
      return testing::AssertionFailure()
             << "Expected one ext_proc test stream ID, got " << ids.size();
    }
    const std::string id(ids[0]->value().getStringView());
    // An identical response from an earlier action must not satisfy this wait. Events completed
    // during action() are retained even before waitFor() starts.
    events_->clear();
    const auto before = time_source_.monotonicTime();
    action();
    if (!events_->waitFor(id, event, response)) {
      return testing::AssertionFailure() << "Timed out waiting for ext_proc callback completion";
    }
    if (before != time_source_.monotonicTime()) {
      return testing::AssertionFailure()
             << "Simulated time advanced while waiting for ext_proc I/O";
    }
    return testing::AssertionSuccess();
  }

private:
  TimeSource& time_source_;
  std::shared_ptr<ProcessingEventObserver> events_;
  ObservedFilterFactoryRegistration factory_registration_;
};

ExtProcTestServer::ExtProcTestServer(TimeSource& time_source)
    : impl_(std::make_unique<Impl>(time_source)) {}

ExtProcTestServer::~ExtProcTestServer() = default;

testing::AssertionResult ExtProcTestServer::sendResponse(FakeStream& stream,
                                                         const ProcessingResponse& response) {
  return impl_->waitForEvent(
      stream, ProcessingEvent::Message, [&] { stream.sendGrpcMessage(response); }, &response);
}

testing::AssertionResult ExtProcTestServer::startStream(FakeStream& stream, bool send_headers) {
  if (send_headers) {
    return impl_->waitForEvent(stream, ProcessingEvent::InitialMetadata,
                               [&] { stream.startGrpcStream(); });
  }
  stream.startGrpcStream(false);
  return testing::AssertionSuccess();
}

testing::AssertionResult ExtProcTestServer::finishStream(FakeStream& stream,
                                                         Grpc::Status::GrpcStatus status) {
  return impl_->waitForEvent(stream, ProcessingEvent::RemoteClose,
                             [&] { stream.finishGrpcStream(status); });
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
