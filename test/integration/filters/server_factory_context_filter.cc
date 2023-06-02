#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "source/common/grpc/typed_async_client.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/proto/helloworld.pb.h"
#include "test/integration/filters/server_factory_context_filter_config.pb.h"
#include "test/integration/filters/server_factory_context_filter_config.pb.validate.h"

namespace Envoy {

using test::integration::filters::TestRequest;
using test::integration::filters::TestResponse;
using ResponsePtr = std::unique_ptr<helloworld::HelloReply>;

// A test filter that is created from server factory context.
class ServerFactoryContextFilter
    : public Http::PassThroughFilter,
      public Grpc::AsyncStreamCallbacks<helloworld::HelloReply> {
public:
  ServerFactoryContextFilter(Grpc::AsyncClientManager& client_manager, Stats::Scope& scope,
                             const envoy::config::core::v3::GrpcService& grpc_service)
      : client_manager_(client_manager), scope_(scope),
        client_(client_manager_.getOrCreateRawAsyncClient(grpc_service, scope_, true)), method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {}

  void startStream() {
    Http::AsyncClient::StreamOptions options;
    stream_ = client_.start(*method_descriptor_, *this, options);
  }

  // void send(helloworld::HelloRequest&& request, bool end_stream) override;
  // // Close the stream. This is idempotent and will return true if we
  // // actually closed it.
  // bool close() override;

  // AsyncStreamCallbacks
  void onReceiveMessage(ResponsePtr&& message) override;

  // RawAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {}
  // const StreamInfo::StreamInfo& streamInfo() const override { return stream_.streamInfo(); }

  void send(helloworld::HelloRequest&& request, bool end_stream) {
    stream_->sendMessage(std::move(request), end_stream);
  }

  void close() {
    if (stream_!= nullptr && !stream_closed_) {
      stream_->closeStream();
      stream_closed_ = true;
      stream_->resetStream();
    }
  }

private:
  Grpc::AsyncClientManager& client_manager_;
  Stats::Scope& scope_;
  Grpc::AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> client_;
  const Protobuf::MethodDescriptor* method_descriptor_;
  Grpc::AsyncStream<helloworld::HelloRequest> stream_;
  bool stream_closed_ = false;
};

class ServerFactoryContextFilterFactory : public Extensions::HttpFilters::Common::FactoryBase<
                                       test::integration::filters::ServerFactoryContextFilterConfig> {
public:
  ServerFactoryContextFilterFactory() : FactoryBase("server-factory-context-filter") {}

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const test::integration::filters::ServerFactoryContextFilterConfig&,
                                    const std::string&,
                                    Server::Configuration::FactoryContext&) override {

    return nullptr;
  }

  Http::FilterFactoryCb createFilterServerFactoryFromProtoTyped(
      const test::integration::filters::ServerFactoryContextFilterConfig& proto_config,
      const std::string&, Server::Configuration::ServerFactoryContext& server_context) override {

    return [&server_context, grpc_service = proto_config.grpc_service()](
               Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<ServerFactoryContextFilter>(
          server_context.clusterManager().grpcAsyncClientManager(), server_context.scope(),
          grpc_service));
    };
  }
};

REGISTER_FACTORY(ServerFactoryContextFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
} // namespace Envoy
