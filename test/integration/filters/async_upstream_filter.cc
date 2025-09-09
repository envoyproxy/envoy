

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/async_upstream_filter.pb.h"
#include "test/integration/filters/async_upstream_filter.pb.validate.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class AsyncUpstreamFilter : public Http::PassThroughFilter,
                            public Http::AsyncClient::StreamCallbacks {
public:
  AsyncUpstreamFilter(Upstream::ClusterManager& cluster_manager)
      : cluster_manager_(cluster_manager) {}

  absl::string_view clusterName() {
    Router::RouteConstSharedPtr route = decoder_callbacks_->route();
    RELEASE_ASSERT(route != nullptr, "no error handling in AsyncUpstreamFilter yet");
    const Router::RouteEntry* route_entry = route->routeEntry();
    RELEASE_ASSERT(route_entry != nullptr, "no error handling in AsyncUpstreamFilter yet");
    return route_entry->clusterName();
  }

  Http::AsyncClient& asyncClient(absl::string_view cluster_name) {
    Upstream::ThreadLocalCluster* thread_local_cluster =
        cluster_manager_.getThreadLocalCluster(cluster_name);
    RELEASE_ASSERT(thread_local_cluster != nullptr, "no error handling in AsyncUpstreamFilter yet");
    return thread_local_cluster->httpAsyncClient();
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override {
    RELEASE_ASSERT(end_stream, "AsyncUpstreamFilter doesn't support request body or trailers yet");
    absl::string_view cluster_name = clusterName();
    Http::AsyncClient& async_client = asyncClient(cluster_name);
    sendHeaders(async_client, headers, end_stream);
    return Http::FilterHeadersStatus::StopIteration;
  }

  void sendHeaders(Http::AsyncClient& async_client, Http::RequestHeaderMap& headers,
                   bool end_stream) {
    stream_ = async_client.start(*this, Http::AsyncClient::StreamOptions());
    stream_->sendHeaders(headers, end_stream);
  }

  // StreamCallbacks
  void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override {
    decoder_callbacks_->encodeHeaders(std::move(headers), end_stream, "async_upstream_filtered");
  }
  void onData(Buffer::Instance& buffer, bool end_stream) override {
    decoder_callbacks_->encodeData(buffer, end_stream);
  }
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override {
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  }
  void onComplete() override { stream_ = nullptr; }
  void onReset() override {}

  ~AsyncUpstreamFilter() {
    if (stream_) {
      stream_->reset();
    }
  }

private:
  Upstream::ClusterManager& cluster_manager_;
  Http::AsyncClient::Stream* stream_{nullptr};
};

class AsyncUpstreamFilterFactory : public Extensions::HttpFilters::Common::DualFactoryBase<
                                       test::integration::filters::AsyncUpstreamFilterConfig> {
public:
  AsyncUpstreamFilterFactory() : DualFactoryBase("envoy.test.async_upstream") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const test::integration::filters::AsyncUpstreamFilterConfig&,
                                    const std::string&, DualInfo,
                                    Server::Configuration::ServerFactoryContext& context) override {
    return [&cluster_manager =
                context.clusterManager()](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<AsyncUpstreamFilter>(cluster_manager));
    };
  };
};

using UpstreamAsyncUpstreamFilterFactory = AsyncUpstreamFilterFactory;

REGISTER_FACTORY(AsyncUpstreamFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamAsyncUpstreamFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace Envoy
