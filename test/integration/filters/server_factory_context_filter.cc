#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "source/common/grpc/typed_async_client.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/server_factory_context_filter_config.pb.h"
#include "test/integration/filters/server_factory_context_filter_config.pb.validate.h"
#include "test/proto/helloworld.pb.h"

namespace Envoy {

using ResponsePtr = std::unique_ptr<helloworld::HelloReply>;
using FilterConfigSharedPtr =
    std::shared_ptr<const test::integration::filters::ServerFactoryContextFilterConfig>;

class FilterCallbacks {
public:
  virtual ~FilterCallbacks() = default;
  virtual void onComplete() PURE;
};

class TestGrpcClient : public Grpc::AsyncStreamCallbacks<helloworld::HelloReply> {
public:
  TestGrpcClient(Server::Configuration::ServerFactoryContext& context,
                 const envoy::config::core::v3::GrpcService& grpc_service)
      : client_(context.clusterManager()
                    .grpcAsyncClientManager()
                    .getOrCreateRawAsyncClient(grpc_service, context.scope(), true)
                    .value()),
        method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {}

  // AsyncStreamCallbacks
  void onReceiveMessage(ResponsePtr&&) override { filter_callback_->onComplete(); }

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
    stream_closed_ = true;
    filter_callback_->onComplete();
  }

  void startStream() {
    Http::AsyncClient::StreamOptions options;
    stream_ = client_.start(*method_descriptor_, *this, options);
  }

  void sendMessage(FilterCallbacks& callbacks) {
    filter_callback_ = &callbacks;
    helloworld::HelloRequest request;
    request.set_name("hello");
    send(std::move(request), false);
  }

  bool isStreamClosed() { return stream_closed_; }

  void close() {
    if (stream_ != nullptr && !stream_closed_) {
      stream_->closeStream();
      stream_closed_ = true;
      stream_->resetStream();
    }
  }

private:
  void send(helloworld::HelloRequest&& request, bool end_stream) {
    stream_->sendMessage(std::move(request), end_stream);
  }
  Grpc::AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> client_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Grpc::AsyncStream<helloworld::HelloRequest> stream_;
  bool stream_closed_ = false;
  FilterCallbacks* filter_callback_;
};

// A test filter that is created from server factory context. This filter communicate with
// external server via gRPC.
class ServerFactoryContextFilter : public Http::PassThroughFilter, public FilterCallbacks {
public:
  ServerFactoryContextFilter(FilterConfigSharedPtr config,
                             Server::Configuration::ServerFactoryContext& context)
      : filter_config_(std::move(config)), context_(context),
        test_client_(std::make_unique<TestGrpcClient>(context_, filter_config_->grpc_service())) {}

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    test_client_->startStream();
    if (!test_client_->isStreamClosed()) {
      test_client_->sendMessage(*this);
    } else {
      return Http::FilterHeadersStatus::Continue;
    }
    return Http::FilterHeadersStatus::StopIteration;
  }

  void onComplete() override {
    if (!filter_chain_continued_) {
      filter_chain_continued_ = true;
      decoder_callbacks_->continueDecoding();
    }
  }

  void onDestroy() override { test_client_->close(); }

private:
  FilterConfigSharedPtr filter_config_;
  Server::Configuration::ServerFactoryContext& context_;
  std::unique_ptr<TestGrpcClient> test_client_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  bool filter_chain_continued_ = false;
};

class ServerFactoryContextFilterFactory
    : public Extensions::HttpFilters::Common::FactoryBase<
          test::integration::filters::ServerFactoryContextFilterConfig> {
public:
  ServerFactoryContextFilterFactory() : FactoryBase("server-factory-context-filter") {}

private:
  // Only the creation from serverFactoryContext is implemented, returns nullptr in
  // `createFilterFactoryFromProtoTyped`
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const test::integration::filters::ServerFactoryContextFilterConfig&, const std::string&,
      Server::Configuration::FactoryContext&) override {
    return nullptr;
  }

  Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const test::integration::filters::ServerFactoryContextFilterConfig& proto_config,
      const std::string&, Server::Configuration::ServerFactoryContext& server_context) override {
    FilterConfigSharedPtr filter_config =
        std::make_shared<test::integration::filters::ServerFactoryContextFilterConfig>(
            proto_config);
    return [&server_context, filter_config = std::move(filter_config)](
               Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(
          std::make_shared<ServerFactoryContextFilter>(filter_config, server_context));
    };
  }
};

REGISTER_FACTORY(ServerFactoryContextFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace Envoy
