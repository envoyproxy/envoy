#pragma once

#include "envoy/http/conn_pool.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/http/protocol.h"
#include "envoy/router/router.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/host_description.h"

#include "common/http/header_map_impl.h"
#include "common/router/router.h"
#include "common/router/upstream_request.h"

#include "extensions/upstreams/http/http/upstream_request.h"

namespace Envoy {

namespace {
void addHeader(Envoy::Http::RequestHeaderMap& header_map, absl::string_view header_name,
               const envoy::config::core::v3::Metadata& metadata, absl::string_view meta_key_0,
               absl::string_view meta_key_1) {
  if (auto filter_metadata = metadata.filter_metadata().find(meta_key_0);
      filter_metadata != metadata.filter_metadata().end()) {
    const ProtobufWkt::Struct& data_struct = filter_metadata->second;
    const auto& fields = data_struct.fields();
    if (auto iter = fields.find(meta_key_1); iter != fields.end()) {
      if (iter->second.kind_case() == ProtobufWkt::Value::kStringValue) {
        header_map.setCopy(Envoy::Http::LowerCaseString(std::string(header_name)),
                           iter->second.string_value());
      }
    }
  }
}
} // namespace

class PerHostHttpUpstream : public Router::GenericUpstream {
public:
  PerHostHttpUpstream(Router::UpstreamToDownstream& upstream_request,
                      Envoy::Http::RequestEncoder* encoder,
                      Upstream::HostDescriptionConstSharedPtr host)
      : sub_upstream_(upstream_request, encoder), host_(host) {}

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override {
    sub_upstream_.encodeData(data, end_stream);
  }

  void encodeMetadata(const Envoy::Http::MetadataMapVector& metadata_map_vector) override {
    sub_upstream_.encodeMetadata(metadata_map_vector);
  }

  Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                             bool end_stream) override {
    auto dup = Envoy::Http::RequestHeaderMapImpl::create();
    Envoy::Http::HeaderMapImpl::copyFrom(*dup, headers);
    dup->setCopy(Envoy::Http::LowerCaseString("X-foo"), "foo-common");
    addHeader(*dup, "X-cluster-foo", host_->cluster().metadata(), "foo", "bar");
    if (host_->metadata() != nullptr) {
      addHeader(*dup, "X-host-foo", *host_->metadata(), "foo", "bar");
    }
    return sub_upstream_.encodeHeaders(*dup, end_stream);
  }

  void encodeTrailers(const Envoy::Http::RequestTrailerMap& trailers) override {
    sub_upstream_.encodeTrailers(trailers);
  }

  void readDisable(bool disable) override { sub_upstream_.readDisable(disable); }

  void resetStream() override { sub_upstream_.resetStream(); }

private:
  Extensions::Upstreams::Http::Http::HttpUpstream sub_upstream_;
  Upstream::HostDescriptionConstSharedPtr host_;
};

class PerHostHttpConnPool : public Extensions::Upstreams::Http::Http::HttpConnPool {
public:
  PerHostHttpConnPool(Upstream::ClusterManager& cm, bool is_connect,
                      const Router::RouteEntry& route_entry,
                      absl::optional<Envoy::Http::Protocol> downstream_protocol,
                      Upstream::LoadBalancerContext* ctx)
      : HttpConnPool(cm, is_connect, route_entry, downstream_protocol, ctx) {}

  void onPoolReady(Envoy::Http::RequestEncoder& callbacks_encoder,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const StreamInfo::StreamInfo& info) override {
    conn_pool_stream_handle_ = nullptr;
    auto upstream = std::make_unique<PerHostHttpUpstream>(callbacks_->upstreamToDownstream(),
                                                          &callbacks_encoder, host);
    callbacks_->onPoolReady(std::move(upstream), host,
                            callbacks_encoder.getStream().connectionLocalAddress(), info);
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
  createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect,
                        const Router::RouteEntry& route_entry,
                        absl::optional<Envoy::Http::Protocol> downstream_protocol,
                        Upstream::LoadBalancerContext* ctx) const override {
    if (is_connect) {
      // This example factory doesn't support terminating CONNECT stream.
      return nullptr;
    }
    auto upstream_http_conn_pool = std::make_unique<PerHostHttpConnPool>(
        cm, is_connect, route_entry, downstream_protocol, ctx);
    return (upstream_http_conn_pool->valid() ? std::move(upstream_http_conn_pool) : nullptr);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
};

} // namespace Envoy