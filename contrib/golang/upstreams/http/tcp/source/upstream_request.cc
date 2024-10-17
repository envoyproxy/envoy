#include "upstream_request.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/router.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

FilterConfig::FilterConfig(
    const envoy::extensions::upstreams::http::tcp::golang::v3alpha::Config proto_config, Dso::TcpUpstreamDsoPtr dso_lib)
    : plugin_name_(proto_config.plugin_name()), so_id_(proto_config.library_id()),
      so_path_(proto_config.library_path()), plugin_config_(proto_config.plugin_config()), dso_lib_(dso_lib){};

FilterConfig::~FilterConfig() {
  if (config_id_ > 0) {
    dso_lib_->envoyGoTcpUpstreamDestroyPluginConfig(config_id_, 0);
  }
}

void FilterConfig::newGoPluginConfig() {
  ENVOY_LOG(debug, "tcp upstream initializing golang filter config");
  std::string buf;
  auto res = plugin_config_.SerializeToString(&buf);
  ASSERT(res, "SerializeToString should always successful");
  auto buf_ptr = reinterpret_cast<unsigned long long>(buf.data());
  auto name_ptr = reinterpret_cast<unsigned long long>(plugin_name_.data());

  config_ = new HttpConfigInternal(weak_from_this());
  config_->plugin_name_ptr = name_ptr;
  config_->plugin_name_len = plugin_name_.length();
  config_->config_ptr = buf_ptr;
  config_->config_len = buf.length();
  config_->is_route_config = 0;

  config_id_ = dso_lib_->envoyGoOnTcpUpstreamConfig(config_);

  if (config_id_ == 0) {
    throw EnvoyException(
        fmt::format("golang filter failed to parse plugin config: {} {}", so_id_, so_path_));
  }

  ENVOY_LOG(debug, "tcp upstream new plugin config, id: {}", config_id_);
}

TcpConnPool::TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster, 
  Upstream::ResourcePriority priority, Upstream::LoadBalancerContext* ctx, const Protobuf::Message& config) {
    conn_pool_data_ = thread_local_cluster.tcpConnPool(priority, ctx);

    envoy::extensions::upstreams::http::tcp::golang::v3alpha::Config c;
    
    ProtobufWkt::Any any;
    any.ParseFromString(config.SerializeAsString());

    auto s = c.ParseFromString(any.value());
    ASSERT(s, "any.value() ParseFromString should always successful");

    std::string config_str;
    auto res = c.plugin_config().SerializeToString(&config_str);
    ASSERT(res, "plugin_config SerializeToString should always successful");

    ENVOY_LOG(debug, "tcp upstream load tcp_upstream_golang library at parse config: {} {}", c.library_id(), c.library_path());

    // loads DSO store a static map and a open handles leak will occur when the filter gets loaded and
    // unloaded.
    // TODO: unload DSO when filter updated.
    auto dso_lib = Dso::DsoManager<Dso::TcpUpstreamDsoImpl>::load(c.library_id(), c.library_path(), c.plugin_name());
    if (dso_lib == nullptr) {
      throw EnvoyException(fmt::format("tcp upstream : load library failed: {} {}", c.library_id(), c.library_path()));
    };

    dynamic_lib_ = dso_lib;

    if (dynamic_lib_) {
      FilterConfigSharedPtr config = std::make_shared<FilterConfig>(c, dynamic_lib_);
      config->newGoPluginConfig();
      config_ = config;
    }
    plugin_name_ = c.plugin_name();
}

void TcpConnPool::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                              Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  Network::Connection& latched_conn = conn_data->connection();
  auto upstream =
      std::make_shared<TcpUpstream>(&callbacks_->upstreamToDownstream(), std::move(conn_data), dynamic_lib_, config_);

  ENVOY_LOG(debug, "tcp upstream get host info: {}", host->cluster().name());

  callbacks_->onPoolReady(upstream, host, latched_conn.connectionInfoProvider(),
                          latched_conn.streamInfo(), {});       
}

TcpUpstream::TcpUpstream(Router::UpstreamToDownstream* upstream_request,
                         Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream, Dso::TcpUpstreamDsoPtr dynamic_lib,
                         FilterConfigSharedPtr config)
    :route_entry_(upstream_request->route().routeEntry()), upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)), dynamic_lib_(dynamic_lib),
    config_(config), req_(new RequestInternal(*this)), encoding_state_(req_->encodingState()), decoding_state_(req_->decodingState()) {
  
  // req is used by go, so need to use raw memory and then it is safe to release at the gc
  // finalize phase of the go object.
  req_->plugin_name.data = config_->pluginName().data();
  req_->plugin_name.len = config_->pluginName().length();

  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);
}

TcpUpstream::~TcpUpstream() {
  auto reason = (decoding_state_.isProcessingInGo() || encoding_state_.isProcessingInGo())
                  ? DestroyReason::Terminate
                  : DestroyReason::Normal;

  dynamic_lib_->envoyGoOnTcpUpstreamDestroy(req_, int(reason));
}

bool TcpUpstream::initRequest() {
  if (req_->configId == 0) {
    req_->setWeakFilter(weak_from_this());
    req_->configId = config_->getConfigId();
    return true;
  }
  return false;
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream encodeData: {}", data.toString());

  initRequest();

  ProcessorState& state = encoding_state_;
  Buffer::Instance& buffer = state.doDataList.push(data);
  auto s = dynamic_cast<processState*>(&state);
  state.setFilterState(FilterState::ProcessingData);

  GoUint64 if_end_stream = dynamic_lib_->envoyGoEncodeData(
    s, end_stream ? 1 : 0, reinterpret_cast<uint64_t>(&buffer), buffer.length());
  if (if_end_stream == static_cast<GoUint64>(EndStreamType::NotEndStream)) {
    end_stream = false;
  } else {
    end_stream = true;
  }

  state.setFilterState(FilterState::Done);
  state.doDataList.moveOut(data);

  upstream_conn_data_->connection().write(data, end_stream);
}

Envoy::Http::Status TcpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                               bool end_stream) {
   
  ENVOY_LOG(debug, "tcp upstream encodeHeaders: {}", headers);

  // Headers should only happen once, so use this opportunity to add the proxy
  // proto header, if configured.

  ASSERT(route_entry_ != nullptr);
  if (route_entry_->connectConfig().has_value()) {
    Buffer::OwnedImpl data;
    const auto& connect_config = route_entry_->connectConfig();
    if (connect_config->has_proxy_protocol_config() &&
        upstream_request_->connection().has_value()) {
      Extensions::Common::ProxyProtocol::generateProxyProtoHeader(
          connect_config->proxy_protocol_config(), *upstream_request_->connection(), data);
    }

    if (data.length() != 0 || end_stream) {
      upstream_conn_data_->connection().write(data, end_stream);
    }
  }

  // TcpUpstream::encodeHeaders is called after the UpstreamRequest is fully initialized. Alsoc use
  // this time to synthesize the 200 response headers downstream to complete the CONNECT handshake.
  Envoy::Http::ResponseHeaderMapPtr headersToDownstream{
      Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Status, "200"}})};
  upstream_request_->decodeHeaders(std::move(headersToDownstream), false);
  return Envoy::Http::okStatus();
}

void TcpUpstream::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  Buffer::OwnedImpl data;
  upstream_conn_data_->connection().write(data, true);
}

  /**
   * Enable half-close semantics on the upstream connection. Reading a remote half-close
   * will not fully close the connection. This is off by default.
   */
void TcpUpstream::enableHalfClose(bool enabled) {
  ASSERT(upstream_conn_data_ != nullptr);
  upstream_conn_data_->connection().enableHalfClose(enabled);
}

void TcpUpstream::readDisable(bool disable) {
  if (upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    return;
  }
  upstream_conn_data_->connection().readDisable(disable);
}

void TcpUpstream::resetStream() {
  upstream_request_ = nullptr;
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void TcpUpstream::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream onUpstreamData data length: {}, end: {}", data.length(), end_stream);

  ProcessorState& state = decoding_state_;
  Buffer::Instance& buffer = state.doDataList.push(data);
  auto s = dynamic_cast<processState*>(&state);
  state.setFilterState(FilterState::ProcessingData);

  GoUint64 status = dynamic_lib_->envoyGoOnUpstreamData(
    s, end_stream ? 1 : 0, reinterpret_cast<uint64_t>(&buffer), buffer.length());

  state.setFilterState(FilterState::Done);
  state.doDataList.moveOut(data);

  if (status == static_cast<GoUint64>(UpstreamDataStatus::UpstreamDataFinish)) { // UpstreamDataFinish 
    end_stream = true;
    upstream_request_->decodeData(data, end_stream);
    return;
  } else if (status == static_cast<GoUint64>(UpstreamDataStatus::UpstreamDataContinue)) { // UpstreamDataContinue
    return;
  } else { // UpstreamDataFailure
    end_stream = true;
    upstream_request_->decodeData(data, end_stream);
    return;
  } 
}

void TcpUpstream::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected && upstream_request_) {
    upstream_request_->onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
  }
}

void TcpUpstream::onAboveWriteBufferHighWatermark() {
  ENVOY_LOG(debug, "tcp upstream onAboveWriteBufferHighWatermark");
  if (upstream_request_) {
    upstream_request_->onAboveWriteBufferHighWatermark();
  }
}

void TcpUpstream::onBelowWriteBufferLowWatermark() {
  ENVOY_LOG(debug, "tcp upstream onBelowWriteBufferLowWatermark");
  if (upstream_request_) {
    upstream_request_->onBelowWriteBufferLowWatermark();
  }
}

CAPIStatus TcpUpstream::copyBuffer(ProcessorState& state, Buffer::Instance* buffer, char* data) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "tcp upstream is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (!state.doDataList.checkExisting(buffer)) {
    ENVOY_LOG(debug, "tcp upstream invoking cgo api at invalid state: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  for (const Buffer::RawSlice& slice : buffer->getRawSlices()) {
    // data is the heap memory of go, and the length is the total length of buffer. So use memcpy is
    // safe.
    memcpy(data, static_cast<const char*>(slice.mem_), slice.len_); // NOLINT(safe-memcpy)
    data += slice.len_;
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus TcpUpstream::drainBuffer(ProcessorState& state, Buffer::Instance* buffer, uint64_t length) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "tcp upstream is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (!state.doDataList.checkExisting(buffer)) {
    ENVOY_LOG(debug, "tcp upstream invoking cgo api at invalid state: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }

  buffer->drain(length);
  return CAPIStatus::CAPIOK;
}

CAPIStatus TcpUpstream::setBufferHelper(ProcessorState& state, Buffer::Instance* buffer,
                                   absl::string_view& value, bufferAction action) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "tcp upstream is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (!state.doDataList.checkExisting(buffer)) {
    ENVOY_LOG(debug, "tcp upstream invoking cgo api at invalid state: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  if (action == bufferAction::Set) {
    buffer->drain(buffer->length());
    buffer->add(value);
  } else if (action == bufferAction::Prepend) {
    buffer->prepend(value);
  } else {
    buffer->add(value);
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus TcpUpstream::getStringValue(int id, uint64_t* value_data, int* value_len) {
  // refer the string to req_->strValue, not deep clone, make sure it won't be freed while reading
  // it on the Go side.
  switch (static_cast<EnvoyValue>(id)) {
  case EnvoyValue::RouteName:
    req_->strValue = upstream_request_->route().virtualHost().routeConfig().name();
    break;
  case EnvoyValue::ClusterName: {
    req_->strValue = route_entry_->clusterName();
    break;
  }
  default:
    RELEASE_ASSERT(false, absl::StrCat("invalid string value id: ", id));
  }

  *value_data = reinterpret_cast<uint64_t>(req_->strValue.data());
  *value_len = req_->strValue.length();
  return CAPIStatus::CAPIOK;
}

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy