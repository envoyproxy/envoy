#pragma once

#include "envoy/extensions/filters/network/reverse_tunnel/v3/reverse_tunnel.pb.h"
#include "envoy/http/codec.h"
#include "envoy/network/filter.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

/**
 * Custom IO handle wrapper for upstream reverse connection sockets.
 * This wrapper prevents premature socket closure to allow socket reuse
 * for reverse connections, replacing the need for setSocketReused().
 */
class UpstreamReverseConnectionIOHandle : public Network::IoHandle {
public:
  UpstreamReverseConnectionIOHandle(Network::IoHandlePtr&& wrapped_handle,
                                    std::function<void()> on_close_callback = nullptr);
  ~UpstreamReverseConnectionIOHandle() override = default;

  // Network::IoHandle
  os_fd_t fdDoNotUse() const override;
  Api::IoCallUint64Result close() override;
  bool isOpen() const override;
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slices) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slices) override;
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slices, int flags,
                                  const Network::Address::Ip* self_ip,
                                  const Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slices,
                                  uint32_t self_port,
                                  const Network::IoHandle::UdpSaveCmsgConfig& save_cmsg_config,
                                  RecvMsgOutput& output) override;
  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   const Network::IoHandle::UdpSaveCmsgConfig& save_cmsg_config,
                                   RecvMsgOutput& output) override;
  bool supportsMmsg() const override;
  bool supportsUdpGro() const override;
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  absl::StatusOr<Network::Address::InstanceConstSharedPtr> localAddress() override;
  absl::StatusOr<Network::Address::InstanceConstSharedPtr> peerAddress() override;
  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
  void resetFileEvents() override;
  Network::IoHandlePtr duplicate() override;

  // Additional pure virtual methods from IoHandle.
  bool wasConnected() const override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  Api::SysCallIntResult ioctl(unsigned long control_code, void* in_buffer,
                              unsigned long in_buffer_len, void* out_buffer,
                              unsigned long out_buffer_len, unsigned long* bytes_returned) override;
  Api::SysCallIntResult shutdown(int how) override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override;
  absl::optional<uint64_t> congestionWindowInBytes() const override;
  absl::optional<std::string> interfaceName() override;

  // Mark this socket as managed by the reverse connection system.
  void markAsReverseTunnelSocket() { is_reverse_tunnel_socket_ = true; }
  bool isReverseTunnelSocket() const { return is_reverse_tunnel_socket_; }

private:
  Network::IoHandlePtr wrapped_handle_;
  std::function<void()> on_close_callback_;
  bool is_reverse_tunnel_socket_ = false;
  bool close_called_ = false;
};

/**
 * Configuration for the reverse tunnel network filter.
 */
class ReverseTunnelFilterConfig {
public:
  ReverseTunnelFilterConfig(
      const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config);

  std::chrono::milliseconds pingInterval() const { return ping_interval_; }
  bool autoCloseConnections() const { return auto_close_connections_; }
  const std::string& requestPath() const { return request_path_; }
  const std::string& requestMethod() const { return request_method_; }

  // Validation configuration accessors.
  const std::string& nodeIdFilterStateKey() const { return node_id_filter_state_key_; }
  const std::string& clusterIdFilterStateKey() const { return cluster_id_filter_state_key_; }
  const std::string& tenantIdFilterStateKey() const { return tenant_id_filter_state_key_; }

private:
  const std::chrono::milliseconds ping_interval_;
  const bool auto_close_connections_;
  const std::string request_path_;
  const std::string request_method_;

  // Filter state keys for validation.
  const std::string node_id_filter_state_key_;
  const std::string cluster_id_filter_state_key_;
  const std::string tenant_id_filter_state_key_;
};

using ReverseTunnelFilterConfigSharedPtr = std::shared_ptr<ReverseTunnelFilterConfig>;

/**
 * Network filter that handles reverse tunnel connection acceptance/rejection.
 * This filter processes HTTP requests to a specific endpoint and uses
 * HTTP headers to receive required identifiers.
 *
 * The filter operates as a terminal filter when processing reverse tunnel requests,
 * meaning it stops the filter chain after processing and manages connection lifecycle.
 */
class ReverseTunnelFilter : public Network::ReadFilter,
                            public Http::ServerConnectionCallbacks,
                            public Logger::Loggable<Logger::Id::filter> {
public:
  ReverseTunnelFilter(ReverseTunnelFilterConfigSharedPtr config, Stats::Scope& stats_scope,
                      Server::OverloadManager& overload_manager);

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Http::ServerConnectionCallbacks
  Http::RequestDecoder& newStream(Http::ResponseEncoder& response_encoder,
                                  bool is_internally_created) override;
  void onGoAway(Http::GoAwayErrorCode) override {}

private:
// Stats definition.
#define ALL_REVERSE_TUNNEL_HANDSHAKE_STATS(COUNTER)                                                \
  COUNTER(parse_error)                                                                             \
  COUNTER(validation_failed)                                                                       \
  COUNTER(accepted)                                                                                \
  COUNTER(rejected)

  struct ReverseTunnelStats {
    ALL_REVERSE_TUNNEL_HANDSHAKE_STATS(GENERATE_COUNTER_STRUCT)
    static ReverseTunnelStats generateStats(const std::string& prefix, Stats::Scope& scope);
  };

  // Process reverse tunnel connection.
  void processAcceptedConnection(absl::string_view node_id, absl::string_view cluster_id,
                                 absl::string_view tenant_id);

  // Validate reverse tunnel request using filter state.
  // Returns true if validation passes or no validation keys are configured.
  bool validateRequestUsingFilterState(absl::string_view node_uuid, absl::string_view cluster_uuid,
                                       absl::string_view tenant_uuid);

  ReverseTunnelFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* read_callbacks_{nullptr};

  // HTTP/1 codec and wiring.
  Http::ServerConnectionPtr codec_;
  Stats::Scope& stats_scope_;
  Server::OverloadManager& overload_manager_;

  // Stats counters.
  ReverseTunnelStats stats_;

  // Per-request decoder to buffer body and respond via encoder.
  class RequestDecoderImpl : public Http::RequestDecoder {
  public:
    RequestDecoderImpl(ReverseTunnelFilter& parent, Http::ResponseEncoder& encoder)
        : parent_(parent), encoder_(encoder),
          stream_info_(parent_.read_callbacks_->connection().streamInfo().timeSource(), nullptr,
                       StreamInfo::FilterState::LifeSpan::Connection) {}

    void decodeHeaders(Http::RequestHeaderMapSharedPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::RequestTrailerMapPtr&&) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override;
    void sendLocalReply(Http::Code code, absl::string_view body,
                        const std::function<void(Http::ResponseHeaderMap& headers)>&,
                        const absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) override;
    StreamInfo::StreamInfo& streamInfo() override;
    AccessLog::InstanceSharedPtrVector accessLogHandlers() override;
    Http::RequestDecoderHandlePtr getRequestDecoderHandle() override;

  private:
    void processIfComplete(bool end_stream);

    ReverseTunnelFilter& parent_;
    Http::ResponseEncoder& encoder_;
    Http::RequestHeaderMapSharedPtr headers_;
    Buffer::OwnedImpl body_;
    bool complete_{false};
    StreamInfo::StreamInfoImpl stream_info_;
  };

  std::unique_ptr<RequestDecoderImpl> active_decoder_;
};

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
