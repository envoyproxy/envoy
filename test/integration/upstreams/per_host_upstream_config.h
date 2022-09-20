#pragma once

#include "envoy/http/conn_pool.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/http/protocol.h"
#include "envoy/router/router.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/host_description.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/router/router.h"
#include "source/common/router/upstream_request.h"
#include "source/extensions/upstreams/http/http/upstream_request.h"

namespace Envoy {

namespace {

/**
 * A helper to add header into header map from metadata. The added value is `metadata[key1][key2]`.
 *
 * @param header_map The mutable header map.
 * @param header_name The target header entry in the `header_map`.
 * @param metadata The source of header value.
 * @param key1 The key to the value in metadata.
 * @param key2 The second key to the value after `key1` is selected.
 */
void addHeader(Envoy::Http::RequestHeaderMap& header_map, absl::string_view header_name,
               const envoy::config::core::v3::Metadata& metadata, absl::string_view key1,
               absl::string_view key2) {
  if (auto filter_metadata = metadata.filter_metadata().find(std::string(key1));
      filter_metadata != metadata.filter_metadata().end()) {
    const ProtobufWkt::Struct& data_struct = filter_metadata->second;
    const auto& fields = data_struct.fields();
    if (auto iter = fields.find(toStdStringView(key2)); // NOLINT(std::string_view)
        iter != fields.end()) {
      if (iter->second.kind_case() == ProtobufWkt::Value::kStringValue) {
        header_map.setCopy(Envoy::Http::LowerCaseString(std::string(header_name)),
                           iter->second.string_value());
      }
    }
  }
}
} // namespace

// The http upstream to remember the host and encode the host metadata and cluster metadata into
// upstream http request.
class PerHostHttpUpstream : public Extensions::Upstreams::Http::Http::HttpUpstream {
public:
  PerHostHttpUpstream(Router::UpstreamToDownstream& upstream_request,
                      Envoy::Http::RequestEncoder* encoder,
                      Upstream::HostDescriptionConstSharedPtr host)
      : HttpUpstream(upstream_request, encoder), host_(host) {}

  Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                             bool end_stream) override {
    auto dup = Envoy::Http::RequestHeaderMapImpl::create();
    Envoy::Http::HeaderMapImpl::copyFrom(*dup, headers);
    dup->setCopy(Envoy::Http::LowerCaseString("X-foo"), "foo-common");
    addHeader(*dup, "X-cluster-foo", host_->cluster().metadata(), "foo", "bar");
    if (host_->metadata() != nullptr) {
      addHeader(*dup, "X-host-foo", *host_->metadata(), "foo", "bar");
    }
    return HttpUpstream::encodeHeaders(*dup, end_stream);
  }

private:
  Upstream::HostDescriptionConstSharedPtr host_;
};

class PerHostHttpConnPool : public Extensions::Upstreams::Http::Http::HttpConnPool {
public:
  PerHostHttpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster, bool is_connect,
                      const Router::RouteEntry& route_entry,
                      absl::optional<Envoy::Http::Protocol> downstream_protocol,
                      Upstream::LoadBalancerContext* ctx)
      : HttpConnPool(thread_local_cluster, is_connect, route_entry, downstream_protocol, ctx) {}

  void onPoolReady(Envoy::Http::RequestEncoder& callbacks_encoder,
                   Upstream::HostDescriptionConstSharedPtr host, StreamInfo::StreamInfo& info,
                   absl::optional<Http::Protocol> protocol) override {
    conn_pool_stream_handle_ = nullptr;
    auto upstream = std::make_unique<PerHostHttpUpstream>(callbacks_->upstreamToDownstream(),
                                                          &callbacks_encoder, host);
    callbacks_->onPoolReady(std::move(upstream), host,
                            callbacks_encoder.getStream().connectionInfoProvider(), info, protocol);
  }
};

/**
 * Config registration for the HttpConnPool. @see Router::GenericConnPoolFactory
 */
class PerHostGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return "envoy.filters.connection_pools.http.per_host"; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ThreadLocalCluster& thread_local_cluster, bool is_connect,
                        const Router::RouteEntry& route_entry,
                        absl::optional<Envoy::Http::Protocol> downstream_protocol,
                        Upstream::LoadBalancerContext* ctx) const override {
    if (is_connect) {
      // This example factory doesn't support terminating CONNECT stream.
      return nullptr;
    }
    auto upstream_http_conn_pool = std::make_unique<PerHostHttpConnPool>(
        thread_local_cluster, is_connect, route_entry, downstream_protocol, ctx);
    return (upstream_http_conn_pool->valid() ? std::move(upstream_http_conn_pool) : nullptr);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
};

} // namespace Envoy
