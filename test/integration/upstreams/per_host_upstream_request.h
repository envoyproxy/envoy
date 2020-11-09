#pragma once

#include <memory>

#include "envoy/http/conn_pool.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/http/protocol.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/host_description.h"

#include "common/http/header_map_impl.h"
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
class PerHostHttpConnPool : public Router::GenericConnPool,
                            public Envoy::Http::ConnectionPool::Callbacks {
public:
  // GenericConnPool
  PerHostHttpConnPool(Upstream::ClusterManager& cm, bool is_connect,
                      const Router::RouteEntry& route_entry,
                      absl::optional<Envoy::Http::Protocol> downstream_protocol,
                      Upstream::LoadBalancerContext* ctx) {
    ASSERT(!is_connect);
    conn_pool_ = cm.httpConnPoolForCluster(route_entry.clusterName(), route_entry.priority(),
                                           downstream_protocol, ctx);
  }
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override;
  bool cancelAnyPendingStream() override;
  absl::optional<Envoy::Http::Protocol> protocol() const override;

  // Http::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Http::RequestEncoder& callbacks_encoder,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const StreamInfo::StreamInfo& info) override;
  Upstream::HostDescriptionConstSharedPtr host() const override { return conn_pool_->host(); }

  bool valid() { return conn_pool_ != nullptr; }

private:
  // Points to the actual connection pool to create streams from.
  Envoy::Http::ConnectionPool::Instance* conn_pool_{};
  // The handle to the upstream request. Non-null if there is pending stream to create.
  Envoy::Http::ConnectionPool::Cancellable* conn_pool_stream_handle_{};
  Router::GenericConnectionPoolCallbacks* callbacks_{};
};

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

  // Copy, mutate and pass downstream headers to sub upstream.
  void encodeHeaders(const Envoy::Http::RequestHeaderMap& headers, bool end_stream) override {
    auto dup = Envoy::Http::RequestHeaderMapImpl::create();
    Envoy::Http::HeaderMapImpl::copyFrom(*dup, headers);
    dup->setCopy(Envoy::Http::LowerCaseString("X-foo"), "foo-common");
    addHeader(*dup, "X-cluster-foo", host_->cluster().metadata(), "foo", "bar");
    if (host_->metadata() != nullptr) {
      addHeader(*dup, "X-host-foo", *host_->metadata(), "foo", "bar");
    }
    sub_upstream_.encodeHeaders(*dup, end_stream);
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

} // namespace Envoy