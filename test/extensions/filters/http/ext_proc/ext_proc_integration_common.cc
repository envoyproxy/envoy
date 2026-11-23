// Changing the default behavior of ext_proc is generally not allowed. While you may add tests, you
// generally should not change or remove existing tests.

#include "test/extensions/filters/http/ext_proc/ext_proc_integration_common.h"

#include <chrono>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.pb.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/common/ext_proc/grpc_client_impl.h"
#include "source/extensions/filters/http/ext_proc/config.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/test_common/struct_matchers.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Contains;
using testing::Ge;
using testing::Key;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using Envoy::Protobuf::Any;
using Envoy::Protobuf::MapPair;
using namespace std::chrono_literals;

// Observes completed callbacks, rather than inferring them from statistics. In particular,
// IntegrationTestServer::waitForCounter advances simulated time while polling, which can expire
// the message being awaited before Google gRPC's completion thread has delivered it.
class ProcessingEventObserver {
public:
  std::shared_ptr<absl::Notification> expect(const std::string& stream_id, ProcessingEvent event,
                                             const ProcessingResponse* response) {
    absl::MutexLock lock(&mutex_);
    ASSERT(notification_ == nullptr);
    stream_id_ = stream_id;
    event_ = event;
    expected_response_ = response ? std::make_optional(*response) : std::nullopt;
    notification_ = std::make_shared<absl::Notification>();
    return notification_;
  }

  void complete(const std::string& stream_id, ProcessingEvent event,
                const ProcessingResponse* response = nullptr) {
    absl::MutexLock lock(&mutex_);
    if (notification_ && !notification_->HasBeenNotified() && stream_id == stream_id_ &&
        event == event_ &&
        (!expected_response_ ||
         (response && TestUtility::protoEqual(*response, *expected_response_)))) {
      notification_->Notify();
    }
  }

  void clear() {
    absl::MutexLock lock(&mutex_);
    notification_.reset();
  }

private:
  absl::Mutex mutex_;
  std::string stream_id_ ABSL_GUARDED_BY(mutex_);
  ProcessingEvent event_ ABSL_GUARDED_BY(mutex_) = ProcessingEvent::Message;
  std::optional<ProcessingResponse> expected_response_ ABSL_GUARDED_BY(mutex_);
  std::shared_ptr<absl::Notification> notification_ ABSL_GUARDED_BY(mutex_);
};

namespace {

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
      observer->complete(stream_id, ProcessingEvent::Message, &response);
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

class ObservedFilter : public Filter {
public:
  ObservedFilter(const FilterConfigSharedPtr& config, ClientBasePtr&& client,
                 std::shared_ptr<ProcessingEventObserver> observer)
      : Filter(config, std::move(client)), observer_(std::move(observer)) {}

  void onDestroy() override {
    auto observer = observer_;
    Filter::onDestroy();
    observer->complete("", ProcessingEvent::FilterDestroyed);
  }

private:
  const std::shared_ptr<ProcessingEventObserver> observer_;
};

// Keep the production filter, processor stream, and both real gRPC clients. Only their callback
// delivery is observed, so waits for those callbacks do not need to advance simulated time.
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
      callbacks.addStreamFilter(std::make_shared<ObservedFilter>(
          config,
          std::make_unique<ObservedProcessorClient>(
              context.clusterManager().grpcAsyncClientManager(), context.scope(), observer),
          observer));
    };
  }

  const std::shared_ptr<ProcessingEventObserver> observer_;
};

} // namespace

// Replace both names together: the deprecated name otherwise keeps the original factory's
// config types registered. Restore it before InjectFactory rebuilds the type map on destruction.
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

ExtProcIntegrationTest::ExtProcIntegrationTest()
    : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()),
      processing_events_(std::make_shared<ProcessingEventObserver>()),
      observed_filter_factory_(
          std::make_unique<ObservedFilterFactoryRegistration>(processing_events_)) {}

ExtProcIntegrationTest::~ExtProcIntegrationTest() = default;

void ExtProcIntegrationTest::waitForProcessingEvent(FakeStream& stream, ProcessingEvent event,
                                                    const std::function<void()>& action,
                                                    const ProcessingResponse* response) {
  // Correlate the callback with this exact stream, including per-route service overrides.
  const auto ids = stream.headers().get(Http::LowerCaseString("x-ext-proc-test-stream-id"));
  ASSERT_EQ(ids.size(), 1);
  const std::string id =
      event == ProcessingEvent::FilterDestroyed ? "" : std::string(ids[0]->value().getStringView());
  auto done = processing_events_->expect(id, event, response);
  const auto before = simTime().monotonicTime();
  action();
  const bool completed =
      done->WaitForNotificationWithTimeout(absl::FromChrono(TestUtility::DefaultTimeout));
  processing_events_->clear();
  ASSERT_TRUE(completed) << "Timed out waiting for ext_proc callback completion";
  EXPECT_EQ(before, simTime().monotonicTime());
}

void ExtProcIntegrationTest::sendGrpcResponse(FakeStream& stream,
                                              const ProcessingResponse& response) {
  waitForProcessingEvent(
      stream, ProcessingEvent::Message, [&] { stream.sendGrpcMessage(response); }, &response);
}

void ExtProcIntegrationTest::startGrpcStream(FakeStream& stream, bool send_headers) {
  if (send_headers) {
    waitForProcessingEvent(stream, ProcessingEvent::InitialMetadata,
                           [&] { stream.startGrpcStream(); });
  } else {
    stream.startGrpcStream(false);
  }
}

void ExtProcIntegrationTest::finishGrpcStream(FakeStream& stream, Grpc::Status::GrpcStatus status) {
  waitForProcessingEvent(stream, ProcessingEvent::RemoteClose,
                         [&] { stream.finishGrpcStream(status); });
}

void ExtProcIntegrationTest::destroyFilter(const std::function<void()>& action) {
  waitForProcessingEvent(*processor_stream_, ProcessingEvent::FilterDestroyed, action);
}

void ExtProcIntegrationTest::createUpstreams() {
  HttpIntegrationTest::createUpstreams();
  // Create separate "upstreams" for ExtProc gRPC servers
  for (int i = 0; i < grpc_upstream_count_; ++i) {
    grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
  }
}

void ExtProcIntegrationTest::TearDown() {
  if (processor_connection_) {
    ASSERT_TRUE(processor_connection_->close());
    ASSERT_TRUE(processor_connection_->waitForDisconnect());
  }

  if (processor_connection_1_) {
    ASSERT_TRUE(processor_connection_1_->close());
    ASSERT_TRUE(processor_connection_1_->waitForDisconnect());
  }

  if (processor_connection_2_) {
    ASSERT_TRUE(processor_connection_2_->close());
    ASSERT_TRUE(processor_connection_2_->waitForDisconnect());
  }

  cleanupUpstreamAndDownstream();
}

void ExtProcIntegrationTest::addDownstreamExtProcFilter(
    const std::string& cluster_name, FakeUpstream* grpc_upstream,
    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config,
    const std::string& ext_proc_filter_name) {
  setGrpcService(*proto_config.mutable_grpc_service(), cluster_name, grpc_upstream->localAddress());
  // Same default message timeout as initializeConfig() gives the single filter, see the comment
  // there.
  if (!proto_config.has_message_timeout()) {
    proto_config.mutable_message_timeout()->set_seconds(5);
  }
  envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter ext_proc_filter;
  ext_proc_filter.set_name(ext_proc_filter_name);
  std::ignore = ext_proc_filter.mutable_typed_config()->PackFrom(proto_config);
  config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
}

void ExtProcIntegrationTest::initializeConfig(
    ConfigOptions config_option, const std::vector<std::pair<int, int>>& cluster_endpoints) {
  int total_cluster_endpoints = 0;
  std::for_each(
      cluster_endpoints.begin(), cluster_endpoints.end(),
      [&total_cluster_endpoints](const auto& item) { total_cluster_endpoints += item.second; });
  ASSERT_EQ(total_cluster_endpoints, grpc_upstream_count_);

  config_helper_.addConfigModifier([this, cluster_endpoints, config_option](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
    ConfigHelper::setHttp2(*(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

    // Clusters for ExtProc gRPC servers, starting by copying an existing
    // cluster
    for (const auto& [cluster_id, endpoint_count] : cluster_endpoints) {
      auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
      server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      std::string cluster_name = absl::StrCat("ext_proc_server_", cluster_id);
      server_cluster->set_name(cluster_name);
      server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
      ASSERT_EQ(server_cluster->load_assignment().endpoints_size(), 1);
      auto* endpoints = server_cluster->mutable_load_assignment()->mutable_endpoints(0);
      ASSERT_EQ(endpoints->lb_endpoints_size(), 1);
      for (int i = 1; i < endpoint_count; ++i) {
        auto* new_lb_endpoint = endpoints->add_lb_endpoints();
        new_lb_endpoint->MergeFrom(endpoints->lb_endpoints(0));
      }
    }

    const std::string valid_grpc_cluster_name = "ext_proc_server_0";
    std::string ext_proc_filter_name = "envoy.filters.http.ext_proc";
    if (!two_ext_proc_filters_) {
      if (config_option.valid_grpc_server) {
        // Load configuration of the server from YAML and use a helper to add a grpc_service
        // stanza pointing to the cluster that we just made
        setGrpcService(*proto_config_.mutable_grpc_service(), valid_grpc_cluster_name,
                       grpc_upstreams_[0]->localAddress());
        if (per_try_timeout_enabled_) {
          auto* retry_policy = proto_config_.mutable_grpc_service()->mutable_retry_policy();
          retry_policy->mutable_num_retries()->set_value(2);
          retry_policy->mutable_per_try_timeout()->set_seconds(2);
          auto* back_off = retry_policy->mutable_retry_back_off();
          back_off->mutable_base_interval()->set_seconds(1);
          back_off->mutable_max_interval()->set_seconds(2);
        }
      } else {
        // Set up the gRPC service with wrong cluster name and address.
        setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_wrong_server",
                       std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
      }

      switch (config_option.filter_setup) {
      case ConfigOptions::FilterSetup::kNone:
        break;
      case ConfigOptions::FilterSetup::kDownstream: {
        // Construct a configuration proto for our filter and then re-write it
        // to JSON so that we can add it to the overall config
        envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter
            ext_proc_filter;
        ext_proc_filter.set_name(ext_proc_filter_name);
        std::ignore = ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
        config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
      } break;
      case ConfigOptions::FilterSetup::kCompositeMatchOnRequestHeaders: {
        envoy::type::matcher::v3::HttpRequestHeaderMatchInput request_match_input;
        request_match_input.set_header_name("match-header");
        prependExtProcCompositeFilter(request_match_input);
      } break;
      case ConfigOptions::FilterSetup::kCompositeMatchOnResponseHeaders: {
        envoy::type::matcher::v3::HttpResponseHeaderMatchInput response_match_input;
        response_match_input.set_header_name("match-header");
        prependExtProcCompositeFilter(response_match_input);
      } break;
      }
    }

    // Add set_metadata filter to inject dynamic metadata used for testing
    if (config_option.add_metadata) {
      envoy::config::listener::v3::Filter set_metadata_filter;
      std::string set_metadata_filter_name = "envoy.filters.http.set_metadata";
      set_metadata_filter.set_name(set_metadata_filter_name);

      envoy::extensions::filters::http::set_metadata::v3::Config set_metadata_config;
      auto* untyped_md = set_metadata_config.add_metadata();
      untyped_md->set_metadata_namespace("forwarding_ns_untyped");
      untyped_md->set_allow_overwrite(true);
      Protobuf::Struct test_md_val;
      (*test_md_val.mutable_fields())["foo"].set_string_value("value from set_metadata");
      (*untyped_md->mutable_value()) = test_md_val;

      auto* typed_md = set_metadata_config.add_metadata();
      typed_md->set_metadata_namespace("forwarding_ns_typed");
      typed_md->set_allow_overwrite(true);
      envoy::extensions::filters::http::set_metadata::v3::Metadata typed_md_to_stuff;
      typed_md_to_stuff.set_metadata_namespace("typed_value from set_metadata");
      std::ignore = typed_md->mutable_typed_value()->PackFrom(typed_md_to_stuff);

      std::ignore = set_metadata_filter.mutable_typed_config()->PackFrom(set_metadata_config);
      config_helper_.prependFilter(
          MessageUtil::getJsonStringFromMessageOrError(set_metadata_filter));

      // Add filter that dumps streamInfo into headers so we can check our receiving
      // namespaces
      config_helper_.prependFilter(R"EOF(
        name: stream-info-to-headers-filter
        typed_config:
          "@type": type.googleapis.com/test.integration.filters.StreamInfoToHeadersFilterConfig
      )EOF");
    }

    // Add dynamic_metadata_to_headers filter to inject dynamic metadata used for testing
    if (config_option.add_response_processor) {
      simple_filter_config_ = std::make_unique<UniqueSimpleFilterConfig<
          DynamicMetadataToHeadersFilter,
          test::extensions::filters::http::ext_proc::DynamicMetadataToHeadersFilterConfig>>();
      registration_ = std::make_unique<
          Envoy::Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory>>(
          *simple_filter_config_);
      config_helper_.prependFilter(R"EOF(
        name: dynamic-metadata-to-headers-filter
        typed_config:
          "@type": type.googleapis.com/test.extensions.filters.http.ext_proc.DynamicMetadataToHeadersFilterConfig
    )EOF");
    }

    // Add logging test filter only in Envoy gRPC mode.
    // gRPC side stream logging is only supported in Envoy gRPC mode at the moment.
    if (clientType() == Grpc::ClientType::EnvoyGrpc && config_option.add_logging_filter &&
        config_option.valid_grpc_server) {
      LoggingTestFilterConfig logging_filter_config;
      logging_filter_config.set_logging_id(ext_proc_filter_name);
      logging_filter_config.set_upstream_cluster_name(valid_grpc_cluster_name);
      // No need to check the bytes received for observability mode because it is a
      // "send and go" mode.
      logging_filter_config.set_check_received_bytes(!proto_config_.observability_mode());
      logging_filter_config.set_http_rcd("via_upstream");
      if (config_option.logging_filter_config) {
        logging_filter_config.MergeFrom(*config_option.logging_filter_config);
      }

      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter logging_filter;
      logging_filter.set_name("logging-test-filter");
      std::ignore = logging_filter.mutable_typed_config()->PackFrom(logging_filter_config);

      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(logging_filter));
    }

    // Parameterize with defer processing to prevent bit rot as filter made
    // assumptions of data flow, prior relying on eager processing.
  });

  if (config_option.add_response_processor) {
    processing_response_factory_ = std::make_unique<TestOnProcessingResponseFactory>();
    processing_response_factory_registration_ =
        std::make_unique<Envoy::Registry::InjectFactory<OnProcessingResponseFactory>>(
            *processing_response_factory_);
    Protobuf::Struct config;
    proto_config_.mutable_on_processing_response()->set_name("test-on-processing-response");
    std::ignore =
        proto_config_.mutable_on_processing_response()->mutable_typed_config()->PackFrom(config);
  }

  if (config_option.http1_codec) {
    setUpstreamProtocol(Http::CodecType::HTTP1);
    setDownstreamProtocol(Http::CodecType::HTTP1);
  } else {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }
}

void ExtProcIntegrationTest::twoExtProcFiltersFullDuplexConfig() {
  two_ext_proc_filters_ = true;
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap&) {
    // Filter-1
    proto_config_1_.mutable_processing_mode()->Clear();
    auto* processing_mode_1 = proto_config_1_.mutable_processing_mode();
    processing_mode_1->set_request_header_mode(ProcessingMode::SEND);
    processing_mode_1->set_response_header_mode(ProcessingMode::SEND);
    processing_mode_1->set_request_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
    processing_mode_1->set_response_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
    processing_mode_1->set_request_trailer_mode(ProcessingMode::SEND);
    processing_mode_1->set_response_trailer_mode(ProcessingMode::SEND);
    addDownstreamExtProcFilter("ext_proc_server_1", grpc_upstreams_[1], proto_config_1_,
                               "envoy.filters.http.ext_proc_1");
    // Filter-0
    proto_config_.mutable_processing_mode()->Clear();
    auto* processing_mode = proto_config_.mutable_processing_mode();
    processing_mode->set_request_header_mode(ProcessingMode::SEND);
    processing_mode->set_response_header_mode(ProcessingMode::SEND);
    processing_mode->set_request_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
    processing_mode->set_response_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
    processing_mode->set_request_trailer_mode(ProcessingMode::SEND);
    processing_mode->set_response_trailer_mode(ProcessingMode::SEND);
    addDownstreamExtProcFilter("ext_proc_server_0", grpc_upstreams_[0], proto_config_,
                               "envoy.filters.http.ext_proc");
  });
}

void ExtProcIntegrationTest::setPerRouteConfig(Route* route, const ExtProcPerRoute& cfg) {
  Any cfg_any;
  ASSERT_TRUE(cfg_any.PackFrom(cfg));
  route->mutable_typed_per_filter_config()->insert(
      MapPair<std::string, Any>("envoy.filters.http.ext_proc", cfg_any));
}

void ExtProcIntegrationTest::setPerHostConfig(VirtualHost& vh, const ExtProcPerRoute& cfg) {
  Any cfg_any;
  ASSERT_TRUE(cfg_any.PackFrom(cfg));
  vh.mutable_typed_per_filter_config()->insert(
      MapPair<std::string, Any>("envoy.filters.http.ext_proc", cfg_any));
}

void ExtProcIntegrationTest::protocolConfigEncoding(ProcessingRequest& request) {
  protocol_config_encoded_ = false;
  if (request.has_protocol_config()) {
    protocol_config_encoded_ = true;
    protocol_config_ = request.protocol_config();
  }
}

IntegrationStreamDecoderPtr ExtProcIntegrationTest::sendDownstreamRequest(
    std::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers) {
  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  if (modify_headers) {
    (*modify_headers)(headers);
  }
  return codec_client_->makeHeaderOnlyRequest(headers);
}

IntegrationStreamDecoderPtr ExtProcIntegrationTest::sendDownstreamRequestWithBody(
    absl::string_view body,
    std::optional<std::function<void(Http::RequestHeaderMap& headers)>> modify_headers,
    bool add_content_length) {
  auto conn = makeClientConnection(lookupPort("http"));
  codec_client_ = makeHttpConnection(std::move(conn));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);
  headers.setMethod("POST");
  if (modify_headers) {
    (*modify_headers)(headers);
  }

  if (add_content_length) {
    headers.setContentLength(body.size());
  }
  return codec_client_->makeRequestWithBody(headers, std::string(body));
}

IntegrationStreamDecoderPtr
ExtProcIntegrationTest::sendDownstreamRequestWithBodyAndTrailer(absl::string_view body) {
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl headers;
  HttpTestUtility::addDefaultHeaders(headers);

  auto encoder_decoder = codec_client_->startRequest(headers);
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, body, false);
  Http::TestRequestTrailerMapImpl request_trailers{{"x-trailer-foo", "yes"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  return response;
}

void ExtProcIntegrationTest::verifyDownstreamResponse(IntegrationStreamDecoder& response,
                                                      int status_code) {
  ASSERT_TRUE(response.waitForEndStream());
  EXPECT_TRUE(response.complete());
  EXPECT_EQ(std::to_string(status_code), response.headers().getStatusValue());
}

void ExtProcIntegrationTest::handleUpstreamRequest(bool add_content_length, int status_code) {
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  Http::TestResponseHeaderMapImpl response_headers =
      Http::TestResponseHeaderMapImpl{{":status", std::to_string(status_code)}};
  uint64_t content_length = 100;
  if (add_content_length) {
    response_headers.setContentLength(content_length);
  }
  upstream_request_->encodeHeaders(response_headers, false);
  upstream_request_->encodeData(content_length, true);
}

void ExtProcIntegrationTest::verifyChunkedEncoding(
    const Http::RequestOrResponseHeaderMap& headers) {
  EXPECT_EQ(headers.ContentLength(), nullptr);
  EXPECT_THAT(headers, ContainsHeader(Http::Headers::get().TransferEncoding,
                                      Http::Headers::get().TransferEncodingValues.Chunked));
}

void ExtProcIntegrationTest::handleUpstreamRequestWithTrailer() {
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, false);
  upstream_request_->encodeTrailers(Http::TestResponseTrailerMapImpl{{"x-test-trailers", "Yes"}});
}

void ExtProcIntegrationTest::handleUpstreamRequestWithResponse(const Buffer::Instance& all_data,
                                                               uint64_t chunk_size) {
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  // Copy the data so that we don't modify it
  Buffer::OwnedImpl total_response = all_data;
  while (total_response.length() > 0) {
    auto to_move = std::min(total_response.length(), chunk_size);
    Buffer::OwnedImpl chunk;
    chunk.move(total_response, to_move);
    EXPECT_EQ(to_move, chunk.length());
    upstream_request_->encodeData(chunk, false);
  }
  upstream_request_->encodeData(0, true);
}

void ExtProcIntegrationTest::waitForFirstMessage(FakeUpstream& grpc_upstream,
                                                 ProcessingRequest& request) {
  ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
}

void ExtProcIntegrationTest::processGenericMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    std::optional<std::function<bool(const ProcessingRequest&, ProcessingResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  if (first_message) {
    startGrpcStream(*processor_stream_);
  }
  ProcessingResponse response;
  const bool sendReply = !cb || (*cb)(request, response);
  if (sendReply) {
    sendGrpcResponse(*processor_stream_, response);
  }
}

void ExtProcIntegrationTest::processRequestHeadersMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    std::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_request_headers());
  protocolConfigEncoding(request);
  if (first_message) {
    startGrpcStream(*processor_stream_);
  }
  ProcessingResponse response;
  auto* headers = response.mutable_request_headers();
  const bool sendReply = !cb || (*cb)(request.request_headers(), *headers);
  if (sendReply) {
    sendGrpcResponse(*processor_stream_, response);
  }
}

void ExtProcIntegrationTest::processRequestTrailersMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    std::optional<std::function<bool(const HttpTrailers&, TrailersResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_request_trailers());
  if (first_message) {
    startGrpcStream(*processor_stream_);
  }
  ProcessingResponse response;
  auto* body = response.mutable_request_trailers();
  const bool sendReply = !cb || (*cb)(request.request_trailers(), *body);
  if (sendReply) {
    sendGrpcResponse(*processor_stream_, response);
  }
}

void ExtProcIntegrationTest::processResponseHeadersMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    std::optional<std::function<bool(const HttpHeaders&, HeadersResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_response_headers());
  protocolConfigEncoding(request);

  if (first_message) {
    startGrpcStream(*processor_stream_);
  }
  ProcessingResponse response;
  auto* headers = response.mutable_response_headers();
  const bool sendReply = !cb || (*cb)(request.response_headers(), *headers);
  if (sendReply) {
    sendGrpcResponse(*processor_stream_, response);
  }
}

void ExtProcIntegrationTest::processRequestBodyMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    std::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb,
    bool check_downstream_flow_control) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_request_body());
  protocolConfigEncoding(request);

  if (first_message) {
    startGrpcStream(*processor_stream_);
  }

  if (check_downstream_flow_control) {
    // Check the flow control counter in downstream, which is triggered on the request
    // path to ext_proc server (i.e., from side stream).
    test_server_->waitForCounter("http.config_test.downstream_flow_control_paused_reading_total",
                                 Ge(1));
  }

  // Send back the response from ext_proc server.
  ProcessingResponse response;
  auto* body = response.mutable_request_body();
  const bool sendReply = !cb || (*cb)(request.request_body(), *body);
  if (sendReply) {
    sendGrpcResponse(*processor_stream_, response);
  }
}

void ExtProcIntegrationTest::processResponseBodyMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    std::optional<std::function<bool(const HttpBody&, BodyResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_response_body());
  protocolConfigEncoding(request);

  if (first_message) {
    startGrpcStream(*processor_stream_);
  }
  ProcessingResponse response;
  auto* body = response.mutable_response_body();
  const bool sendReply = !cb || (*cb)(request.response_body(), *body);
  if (sendReply) {
    sendGrpcResponse(*processor_stream_, response);
  }
}

void ExtProcIntegrationTest::processStreamedResponseBodyAs(absl::string_view body) {
  bool end_stream = false;
  bool first_chunk = true;
  while (!end_stream) {
    processResponseBodyMessage(*grpc_upstreams_[0], false,
                               [&](const HttpBody& chunk, BodyResponse& body_resp) {
                                 end_stream = chunk.end_of_stream();
                                 body_resp.mutable_response()->mutable_body_mutation()->set_body(
                                     first_chunk ? std::string(body) : "");
                                 first_chunk = false;
                                 return true;
                               });
    if (testing::Test::HasFatalFailure()) {
      return;
    }
  }
}

void ExtProcIntegrationTest::processResponseTrailersMessage(
    FakeUpstream& grpc_upstream, bool first_message,
    std::optional<std::function<bool(const HttpTrailers&, TrailersResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  ASSERT_TRUE(request.has_response_trailers());
  if (first_message) {
    startGrpcStream(*processor_stream_);
  }
  ProcessingResponse response;
  auto* body = response.mutable_response_trailers();
  const bool sendReply = !cb || (*cb)(request.response_trailers(), *body);
  if (sendReply) {
    sendGrpcResponse(*processor_stream_, response);
  }
}

void ExtProcIntegrationTest::processAndRespondImmediately(
    FakeUpstream& grpc_upstream, bool first_message,
    std::optional<std::function<void(ImmediateResponse&)>> cb) {
  ProcessingRequest request;
  if (first_message) {
    ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  if (first_message) {
    startGrpcStream(*processor_stream_);
  }
  ProcessingResponse response;
  auto* immediate = response.mutable_immediate_response();
  if (cb) {
    (*cb)(*immediate);
  }
  sendGrpcResponse(*processor_stream_, response);
}

void ExtProcIntegrationTest::packTwoResponsesInOneMessage(
    FakeUpstream& grpc_upstream, bool immediate_response,
    std::optional<std::function<void(ImmediateResponse&)>> cb) {
  ProcessingRequest request;
  ASSERT_TRUE(grpc_upstream.waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  startGrpcStream(*processor_stream_);

  ProcessingResponse response1;
  if (immediate_response) {
    auto* immediate = response1.mutable_immediate_response();
    if (cb) {
      (*cb)(*immediate);
    }
  } else {
    response1.mutable_request_trailers();
  }
  auto serialized_response1 = Grpc::Common::serializeToGrpcFrame(response1);
  Buffer::OwnedImpl combined_buffer;
  combined_buffer.add(*serialized_response1);
  ProcessingResponse response2;
  response2.mutable_response_trailers();
  auto serialized_response2 = Grpc::Common::serializeToGrpcFrame(response2);
  combined_buffer.add(*serialized_response2);
  processor_stream_->encodeData(combined_buffer, false);
}

// ext_proc server sends back a response to tell Envoy to stop the
// original timer and start a new timer.
void ExtProcIntegrationTest::serverSendNewTimeout(const uint64_t timeout_ms) {
  ProcessingResponse response;
  if (timeout_ms < 1000) {
    response.mutable_override_message_timeout()->set_nanos(timeout_ms * 1000000);
  } else {
    response.mutable_override_message_timeout()->set_seconds(timeout_ms / 1000);
  }
  sendGrpcResponse(*processor_stream_, response);
}

void ExtProcIntegrationTest::newTimeoutWrongConfigTest(const uint64_t timeout_ms) {
  // Set envoy filter timeout to be 200ms.
  proto_config_.mutable_message_timeout()->set_nanos(200000000);
  // Config max_message_timeout proto to enable the new timeout API.
  if (max_message_timeout_ms_) {
    if (max_message_timeout_ms_ < 1000) {
      proto_config_.mutable_max_message_timeout()->set_nanos(max_message_timeout_ms_ * 1000000);
    } else {
      proto_config_.mutable_max_message_timeout()->set_seconds(max_message_timeout_ms_ / 1000);
    }
  }
  initializeConfig();
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(std::nullopt);

  processRequestHeadersMessage(*grpc_upstreams_[0], true,
                               [&](const HttpHeaders&, HeadersResponse&) {
                                 serverSendNewTimeout(timeout_ms);
                                 // 300ms passes the original 200ms deadline; an ignored override
                                 // must not extend it.
                                 simTime().advanceTimeWait(300ms);
                                 // Preserve the late reply, but do not wait for delivery on the
                                 // side stream that the timeout has already reset.
                                 ProcessingResponse late_response;
                                 late_response.mutable_request_headers();
                                 processor_stream_->sendGrpcMessage(late_response);
                                 return false;
                               });
  // Verify the new timer is not started and the original timer timeouts,
  // and downstream receives 504.
  verifyDownstreamResponse(*response, 504);
}

void ExtProcIntegrationTest::addMutationSetHeaders(
    const int count, envoy::service::ext_proc::v3::HeaderMutation& mutation) {
  for (int i = 0; i < count; i++) {
    auto* headers = mutation.add_set_headers();
    auto str = absl::StrCat("x-test-header-internal-", std::to_string(i));
    headers->mutable_append()->set_value(false);
    headers->mutable_header()->set_key(str);
    headers->mutable_header()->set_raw_value(str);
  }
}

void ExtProcIntegrationTest::testWithHeaderMutation(ConfigOptions config_option) {
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("Replace this!", std::nullopt);
  processRequestHeadersMessage(
      *grpc_upstreams_[0], true, [](const HttpHeaders&, HeadersResponse& headers_resp) {
        auto* content_length =
            headers_resp.mutable_response()->mutable_header_mutation()->add_set_headers();
        content_length->mutable_append()->set_value(false);
        content_length->mutable_header()->set_key("content-length");
        content_length->mutable_header()->set_raw_value("13");
        return true;
      });

  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        return true;
      });
  handleUpstreamRequest();
  // Verify that the content length header is removed and chunked encoding is enabled by http1
  // codec.
  verifyChunkedEncoding(upstream_request_->headers());

  EXPECT_EQ(upstream_request_->body().toString(), "Hello, World!");
  verifyDownstreamResponse(*response, 200);
}

// Verify existing content-length header (i.e., no external processor mutation) is removed and
// chunked encoding is enabled.
void ExtProcIntegrationTest::testWithoutHeaderMutation(ConfigOptions config_option) {
  initializeConfig(config_option);
  HttpIntegrationTest::initialize();

  auto response = sendDownstreamRequestWithBody("test!", std::nullopt, /*add_content_length=*/true);
  processRequestHeadersMessage(*grpc_upstreams_[0], true, std::nullopt);
  processRequestBodyMessage(
      *grpc_upstreams_[0], false, [](const HttpBody& body, BodyResponse& body_resp) {
        EXPECT_TRUE(body.end_of_stream());
        auto* body_mut = body_resp.mutable_response()->mutable_body_mutation();
        body_mut->set_body("Hello, World!");
        return true;
      });

  handleUpstreamRequest();
  verifyChunkedEncoding(upstream_request_->headers());

  EXPECT_EQ(upstream_request_->body().toString(), "Hello, World!");
  verifyDownstreamResponse(*response, 200);
}

void ExtProcIntegrationTest::addMutationRemoveHeaders(
    const int count, envoy::service::ext_proc::v3::HeaderMutation& mutation) {
  for (int i = 0; i < count; i++) {
    mutation.add_remove_headers(absl::StrCat("x-test-header-internal-", std::to_string(i)));
  }
}

void ExtProcIntegrationTest::testGetAndFailStream() {
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(std::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  // Fail the stream immediately
  processor_stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "500"}}, true);
  verifyDownstreamResponse(*response, 500);
}

void ExtProcIntegrationTest::testGetAndCloseStream() {
  HttpIntegrationTest::initialize();
  auto response = sendDownstreamRequest(std::nullopt);

  ProcessingRequest request_headers_msg;
  waitForFirstMessage(*grpc_upstreams_[0], request_headers_msg);
  // Just close the stream without doing anything
  startGrpcStream(*processor_stream_);
  finishGrpcStream(*processor_stream_, Grpc::Status::Ok);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

void ExtProcIntegrationTest::testSendDynamicMetadata() {
  Protobuf::Struct test_md_struct;
  (*test_md_struct.mutable_fields())["foo"].set_string_value("value from ext_proc");

  Protobuf::Value md_val;
  *(md_val.mutable_struct_value()) = test_md_struct;

  processGenericMessage(
      *grpc_upstreams_[0], true, [md_val](const ProcessingRequest& req, ProcessingResponse& resp) {
        // Verify the processing request contains the untyped metadata we injected.
        EXPECT_THAT(req.metadata_context().filter_metadata(),
                    Contains(Key("forwarding_ns_untyped")));
        const Protobuf::Struct& fwd_metadata =
            req.metadata_context().filter_metadata().at("forwarding_ns_untyped");
        EXPECT_THAT(fwd_metadata.fields(),
                    UnorderedElementsAre(IsStructString("foo", "value from set_metadata")));

        // Verify the processing request contains the typed metadata we injected.
        EXPECT_THAT(req.metadata_context().typed_filter_metadata(),
                    Contains(Key("forwarding_ns_typed")));
        const Protobuf::Any& fwd_typed_metadata =
            req.metadata_context().typed_filter_metadata().at("forwarding_ns_typed");
        EXPECT_EQ("type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Metadata",
                  fwd_typed_metadata.type_url());
        envoy::extensions::filters::http::set_metadata::v3::Metadata typed_md_from_req;
        std::ignore = fwd_typed_metadata.UnpackTo(&typed_md_from_req);
        EXPECT_EQ("typed_value from set_metadata", typed_md_from_req.metadata_namespace());

        // Spoof the response to contain receiving metadata.
        HeadersResponse headers_resp;
        (*resp.mutable_request_headers()) = headers_resp;
        auto mut_md_fields = resp.mutable_dynamic_metadata()->mutable_fields();
        (*mut_md_fields).emplace("receiving_ns_untyped", md_val);

        return true;
      });
}

void ExtProcIntegrationTest::testSendTypedDynamicMetadata() {
  envoy::extensions::filters::http::set_metadata::v3::Metadata typed_md_to_stuff;
  typed_md_to_stuff.set_metadata_namespace("typed_value from ext_proc");

  Protobuf::Any typed_val;
  EXPECT_TRUE(typed_val.PackFrom(typed_md_to_stuff));

  processGenericMessage(*grpc_upstreams_[0], true,
                        [typed_val](const ProcessingRequest&, ProcessingResponse& resp) {
                          // Spoof the response to contain receiving typed metadata.
                          HeadersResponse headers_resp;
                          (*resp.mutable_request_headers()) = headers_resp;
                          auto mut_typed_md = resp.mutable_typed_dynamic_metadata();
                          (*mut_typed_md)["receiving_ns_typed"] = typed_val;

                          return true;
                        });
}

void ExtProcIntegrationTest::testSidestreamPushbackDownstream(uint32_t body_size,
                                                              bool check_downstream_flow_control) {
  config_helper_.setBufferLimits(1024, 1024);

  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_request_body_mode(ProcessingMode::STREAMED);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  initializeConfig();
  HttpIntegrationTest::initialize();

  std::string body_str = std::string(body_size, 'a');
  auto response = sendDownstreamRequestWithBody(body_str, std::nullopt);

  bool end_stream = false;
  int count = 0;
  while (!end_stream) {
    processRequestBodyMessage(
        *grpc_upstreams_[0], count == 0 ? true : false,
        [&end_stream](const HttpBody& body, BodyResponse&) {
          end_stream = body.end_of_stream();
          return true;
        },
        check_downstream_flow_control);
    count++;
  }
  handleUpstreamRequest();

  verifyDownstreamResponse(*response, 200);
}

void ExtProcIntegrationTest::initializeConfigDuplexStreamed(bool both_direction) {
  config_helper_.setBufferLimits(1024, 1024);
  auto* processing_mode = proto_config_.mutable_processing_mode();
  processing_mode->set_request_header_mode(ProcessingMode::SEND);
  processing_mode->set_request_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
  processing_mode->set_request_trailer_mode(ProcessingMode::SEND);
  if (!both_direction) {
    processing_mode->set_response_header_mode(ProcessingMode::SKIP);
  } else {
    processing_mode->set_response_header_mode(ProcessingMode::SEND);
    processing_mode->set_response_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
    processing_mode->set_response_trailer_mode(ProcessingMode::SEND);
  }

  initializeConfig();
  HttpIntegrationTest::initialize();
}

IntegrationStreamDecoderPtr
ExtProcIntegrationTest::initAndSendDataDuplexStreamedMode(absl::string_view body_sent,
                                                          bool end_of_stream, bool both_direction) {
  initializeConfigDuplexStreamed(both_direction);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestRequestHeaderMapImpl default_headers;
  HttpTestUtility::addDefaultHeaders(default_headers);

  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> encoder_decoder =
      codec_client_->startRequest(default_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, body_sent, end_of_stream);
  return response;
}

void ExtProcIntegrationTest::serverReceiveHeaderReq(ProcessingRequest& header, bool first_message,
                                                    bool response) {
  if (first_message) {
    EXPECT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, processor_connection_));
    EXPECT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  }
  EXPECT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, header));
  if (response) {
    EXPECT_TRUE(header.has_response_headers());
  } else {
    EXPECT_TRUE(header.has_request_headers());
  }
}

void ExtProcIntegrationTest::server1ReceiveHeaderReq(ProcessingRequest& header, bool first_message,
                                                     bool response) {
  if (first_message) {
    EXPECT_TRUE(grpc_upstreams_[1]->waitForHttpConnection(*dispatcher_, processor_connection_1_));
    EXPECT_TRUE(processor_connection_1_->waitForNewStream(*dispatcher_, processor_stream_1_));
  }
  EXPECT_TRUE(processor_stream_1_->waitForGrpcMessage(*dispatcher_, header));
  if (response) {
    EXPECT_TRUE(header.has_response_headers());
  } else {
    EXPECT_TRUE(header.has_request_headers());
  }
}

uint32_t ExtProcIntegrationTest::serverReceiveBodyDuplexStreamed(absl::string_view body_sent,
                                                                 FakeStreamPtr& processor_stream,
                                                                 bool response, bool compare_body) {
  std::string body_received;
  bool end_stream = false;
  uint32_t total_req_body_msg = 0;
  while (!end_stream) {
    ProcessingRequest body_request;
    if (!processor_stream->waitForGrpcMessage(*dispatcher_, body_request)) {
      ADD_FAILURE() << "Timed out waiting for gRPC message in serverReceiveBodyDuplexStreamed";
      return total_req_body_msg;
    }
    if (response) {
      if (body_request.has_response_trailers()) {
        end_stream = true;
      } else {
        if (!body_request.has_response_body()) {
          ADD_FAILURE() << "Expected response body message but got unexpected message type";
          return total_req_body_msg;
        }
        absl::StrAppend(&body_received, body_request.response_body().body());
        end_stream = body_request.response_body().end_of_stream();
        total_req_body_msg++;
      }
    } else {
      if (body_request.has_request_trailers()) {
        end_stream = true;
      } else {
        if (!body_request.has_request_body()) {
          ADD_FAILURE() << "Expected request body message but got unexpected message type";
          return total_req_body_msg;
        }
        absl::StrAppend(&body_received, body_request.request_body().body());
        end_stream = body_request.request_body().end_of_stream();
        total_req_body_msg++;
      }
    }
  }
  EXPECT_TRUE(end_stream);
  if (compare_body) {
    EXPECT_EQ(body_received, body_sent);
  }
  return total_req_body_msg;
}

void ExtProcIntegrationTest::serverSendHeaderResp(bool first_message, bool response) {
  if (first_message) {
    startGrpcStream(*processor_stream_);
  }
  ProcessingResponse response_header;
  HeadersResponse* header_resp;
  if (response) {
    header_resp = response_header.mutable_response_headers();
  } else {
    header_resp = response_header.mutable_request_headers();
  }
  auto* header_mutation = header_resp->mutable_response()->mutable_header_mutation();
  auto* sh = header_mutation->add_set_headers();
  auto* header = sh->mutable_header();
  sh->mutable_append()->set_value(false);
  header->set_key("x-new-header");
  header->set_raw_value("new");
  sendGrpcResponse(*processor_stream_, response_header);
}

void ExtProcIntegrationTest::server1SendHeaderResp(bool first_message, bool response) {
  if (first_message) {
    startGrpcStream(*processor_stream_1_);
  }
  ProcessingResponse response_header;
  HeadersResponse* header_resp;
  if (response) {
    header_resp = response_header.mutable_response_headers();
  } else {
    header_resp = response_header.mutable_request_headers();
  }
  auto* header_mutation = header_resp->mutable_response()->mutable_header_mutation();
  auto* sh = header_mutation->add_set_headers();
  auto* header = sh->mutable_header();
  sh->mutable_append()->set_value(false);
  header->set_key("x-new-header_1");
  header->set_raw_value("new_1");
  sendGrpcResponse(*processor_stream_1_, response_header);
}

void ExtProcIntegrationTest::serverSendBodyRespDuplexStreamed(uint32_t total_resp_body_msg,
                                                              FakeStreamPtr& processor_stream,
                                                              bool end_of_stream, bool response,
                                                              absl::string_view body_sent) {
  for (uint32_t i = 0; i < total_resp_body_msg; i++) {
    ProcessingResponse response_body;
    BodyResponse* body_resp;
    if (response) {
      body_resp = response_body.mutable_response_body();
    } else {
      body_resp = response_body.mutable_request_body();
    }

    auto* body_mut = body_resp->mutable_response()->mutable_body_mutation();
    auto* streamed_response = body_mut->mutable_streamed_response();
    if (!body_sent.empty()) {
      streamed_response->set_body(body_sent);
    } else {
      streamed_response->set_body("r");
    }
    if (end_of_stream) {
      const bool end_of_stream = (i == total_resp_body_msg - 1) ? true : false;
      streamed_response->set_end_of_stream(end_of_stream);
    }
    processor_stream->sendGrpcMessage(response_body);
  }
}

void ExtProcIntegrationTest::serverSendTrailerRespDuplexStreamed(FakeStreamPtr& processor_stream,
                                                                 bool response) {
  ProcessingResponse response_trailer;
  TrailersResponse* trailer_resp;
  if (!response) {
    trailer_resp = response_trailer.mutable_request_trailers();
  } else {
    trailer_resp = response_trailer.mutable_response_trailers();
  }
  auto* header_mutation = trailer_resp->mutable_header_mutation();
  auto* sh = header_mutation->add_set_headers();
  sh->mutable_append()->set_value(false);
  auto* header = sh->mutable_header();
  if (processor_stream == processor_stream_) {
    header->set_key("x-new-trailer");
    header->set_raw_value("new");
  } else {
    header->set_key("x-new-trailer_1");
    header->set_raw_value("new_1");
  }
  processor_stream->sendGrpcMessage(response_trailer);
}

void ExtProcIntegrationTest::prependExtProcCompositeFilter(const Protobuf::Message& match_input) {
  envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter composite_filter;
  composite_filter.set_name("composite");

  envoy::extensions::common::matching::v3::ExtensionWithMatcher extension_with_matcher;
  auto* extension_config = extension_with_matcher.mutable_extension_config();
  extension_config->set_name("composite");
  envoy::extensions::filters::http::composite::v3::Composite composite_config;
  std::ignore = extension_config->mutable_typed_config()->PackFrom(composite_config);

  auto* matcher_tree = extension_with_matcher.mutable_xds_matcher()->mutable_matcher_tree();
  auto* input = matcher_tree->mutable_input();
  input->set_name("match-input");
  std::ignore = input->mutable_typed_config()->PackFrom(match_input);

  envoy::extensions::filters::http::composite::v3::ExecuteFilterAction execute_filter_action;
  auto* typed_config = execute_filter_action.mutable_typed_config();
  typed_config->set_name("envoy.filters.http.ext_proc");
  std::ignore = typed_config->mutable_typed_config()->PackFrom(proto_config_);

  auto& on_match = (*matcher_tree->mutable_exact_match_map()->mutable_map())["match"];
  on_match.mutable_action()->set_name("composite-action");
  std::ignore = on_match.mutable_action()->mutable_typed_config()->PackFrom(execute_filter_action);

  std::ignore = composite_filter.mutable_typed_config()->PackFrom(extension_with_matcher);
  config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(composite_filter),
                               true);
}

void ExtProcIntegrationTest::initializeLogConfig(std::string& access_log_path) {
  config_helper_.addConfigModifier([&](ConfigHelper::HttpConnectionManager& cm) {
    auto* access_log = cm.add_access_log();
    access_log->set_name("accesslog");
    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.set_path(access_log_path);
    auto* json_format = access_log_config.mutable_log_format()->mutable_json_format();

    // Test all three serialization modes.
    (*json_format->mutable_fields())["ext_proc_plain"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:PLAIN)%");
    (*json_format->mutable_fields())["ext_proc_typed"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:TYPED)%");

    // Test field extraction for coverage.
    (*json_format->mutable_fields())["field_request_header_latency"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_header_latency_us)%");
    (*json_format->mutable_fields())["field_request_header_status"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_header_call_status)%");
    (*json_format->mutable_fields())["field_request_body_calls"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_body_call_count)%");
    (*json_format->mutable_fields())["field_request_body_total_latency"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_body_total_latency_us)%");
    (*json_format->mutable_fields())["field_request_body_max_latency"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_body_max_latency_us)%");
    (*json_format->mutable_fields())["field_request_body_last_status"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_body_last_call_status)%");
    (*json_format->mutable_fields())["field_request_trailer_latency"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_trailer_latency_us)%");
    (*json_format->mutable_fields())["field_request_trailer_status"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_trailer_call_status)%");
    (*json_format->mutable_fields())["field_response_header_latency"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_header_latency_us)%");
    (*json_format->mutable_fields())["field_response_header_status"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_header_call_status)%");
    (*json_format->mutable_fields())["field_response_body_calls"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_body_call_count)%");
    (*json_format->mutable_fields())["field_response_body_total_latency"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_body_total_latency_us)%");
    (*json_format->mutable_fields())["field_response_body_max_latency"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_body_max_latency_us)%");
    (*json_format->mutable_fields())["field_response_body_last_status"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_body_last_call_status)%");
    (*json_format->mutable_fields())["field_response_trailer_latency"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_trailer_latency_us)%");
    (*json_format->mutable_fields())["field_response_trailer_status"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_trailer_call_status)%");
    (*json_format->mutable_fields())["field_bytes_sent"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:bytes_sent)%");
    (*json_format->mutable_fields())["field_bytes_received"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:bytes_received)%");
    (*json_format->mutable_fields())["field_request_header_effect"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_header_processing_effect)%");
    (*json_format->mutable_fields())["field_request_body_effect"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_body_processing_effect)%");
    (*json_format->mutable_fields())["field_request_trailer_effect"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_trailer_processing_effect)%");
    (*json_format->mutable_fields())["field_response_header_effect"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_header_processing_effect)%");
    (*json_format->mutable_fields())["field_response_body_effect"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_body_processing_effect)%");
    (*json_format->mutable_fields())["field_response_trailer_effect"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:response_trailer_processing_effect)%");
    (*json_format->mutable_fields())["failed_open_field"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:failed_open)%");
    (*json_format->mutable_fields())["received_immediate_response_field"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:received_immediate_response)%");
    (*json_format->mutable_fields())["field_grpc_status_before_first_call"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:grpc_status_before_first_call)%");

    // Test non-existent field for coverage
    (*json_format->mutable_fields())["field_non_existent"].set_string_value(
        "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:non_existent_field)%");

    std::ignore = access_log->mutable_typed_config()->PackFrom(access_log_config);
  });
}

void ExtProcIntegrationTest::performStandAloneModeOverrideNormal(
    envoy::extensions::filters::http::ext_proc::v3::ProcessingMode_BodySendMode initial_body_mode,
    bool delay_header_response) {
  proto_config_.mutable_processing_mode()->set_request_body_mode(initial_body_mode);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  proto_config_.set_allow_mode_override(true);
  initializeConfig();
  HttpIntegrationTest::initialize();

  std::string body_str = "hello world";
  auto response = sendDownstreamRequestWithBody(body_str, std::nullopt);

  // Process request headers message and send back only mode_override (no response_case set).
  processGenericMessage(
      *grpc_upstreams_[0], true, [](const ProcessingRequest& req, ProcessingResponse& resp) {
        EXPECT_TRUE(req.has_request_headers());
        resp.mutable_mode_override()->set_request_body_mode(ProcessingMode::FULL_DUPLEX_STREAMED);
        resp.mutable_mode_override()->set_request_trailer_mode(ProcessingMode::SEND);
        return true;
      });

  ProcessingRequest request;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  EXPECT_TRUE(request.has_request_body());
  EXPECT_TRUE(request.request_body().end_of_stream());

  if (delay_header_response) {
    // 400ms is twice the caller's 200ms timeout: FULL_DUPLEX_STREAMED must have disabled
    // the message timer even while the header response is still pending.
    simTime().advanceTimeWait(400ms);
  }

  // The server sends back the header response first:
  ProcessingResponse resp_headers;
  resp_headers.mutable_request_headers();
  processor_stream_->sendGrpcMessage(resp_headers);

  // Then, it streams back the body responses:
  ProcessingResponse resp_body;
  auto* streamed_response = resp_body.mutable_request_body()
                                ->mutable_response()
                                ->mutable_body_mutation()
                                ->mutable_streamed_response();
  streamed_response->set_body(body_str);
  streamed_response->set_end_of_stream(true);
  processor_stream_->sendGrpcMessage(resp_body);

  handleUpstreamRequest();
  verifyDownstreamResponse(*response, 200);
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
