#include "upstream_request.h"

#include <cstdint>
#include <cstdlib>
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

#include "processor_state.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

BridgeConfig::BridgeConfig(
    const envoy::extensions::upstreams::http::tcp::golang::v3alpha::Config proto_config,
    Dso::HttpTcpBridgeDsoPtr dso_lib)
    : plugin_name_(proto_config.plugin_name()), so_id_(proto_config.library_id()),
      so_path_(proto_config.library_path()), plugin_config_(proto_config.plugin_config()),
      dso_lib_(dso_lib){};

BridgeConfig::~BridgeConfig() {
  if (config_id_ > 0) {
    dso_lib_->envoyGoHttpTcpBridgeDestroyPluginConfig(config_id_);
  }
}

void BridgeConfig::newGoPluginConfig() {
  ENVOY_LOG(debug, "golang http-tcp bridge initializing golang filter config");
  std::string buf;
  auto res = plugin_config_.SerializeToString(&buf);
  ASSERT(res, "SerializeToString should always success");
  auto buf_ptr = reinterpret_cast<unsigned long long>(buf.data());
  auto name_ptr = reinterpret_cast<unsigned long long>(plugin_name_.data());

  // plugin_name_ptr in httpConfig
  plugin_name_ptr = name_ptr;
  // plugin_name_len in httpConfig
  plugin_name_len = plugin_name_.length();
  // config_ptr in httpConfig
  config_ptr = buf_ptr;
  // config_len in httpConfig
  config_len = buf.length();

  config_id_ = dso_lib_->envoyGoHttpTcpBridgeOnConfig(this);

  if (config_id_ == 0) {
    PANIC(fmt::format("golang http-tcp bridge failed to parse plugin config: {} {}", so_id_,
                      so_path_));
  }

  ENVOY_LOG(debug, "golang http-tcp bridge new plugin config, id: {}", config_id_);
}

TcpConnPool::TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
                         Upstream::ResourcePriority priority, Upstream::LoadBalancerContext* ctx,
                         const Protobuf::Message& config) {
  conn_pool_data_ = thread_local_cluster.tcpConnPool(priority, ctx);

  envoy::extensions::upstreams::http::tcp::golang::v3alpha::Config c;
  std::string serialized_config;
  if (!config.SerializeToString(&serialized_config)) {
    PANIC(fmt::format(
        "golang http-tcp bridge: failed to serialize input Protobuf::Message to string."));
  }
  if (!c.ParseFromString(serialized_config)) {
    PANIC(fmt::format(
        "golang http-tcp bridge: failed to parse serialized data into target config object."));
  }

  ENVOY_LOG(debug, "golang http-tcp bridge load library at parse config: {} {}", c.library_id(),
            c.library_path());

  dynamic_lib_ = Dso::DsoManager<Dso::HttpTcpBridgeDsoImpl>::load(c.library_id(), c.library_path(),
                                                                  c.plugin_name());
  if (dynamic_lib_ == nullptr) {
    PANIC(fmt::format("golang http-tcp bridge: load library failed: {} {}", c.library_id(),
                      c.library_path()));
  };

  BridgeConfigSharedPtr conf = std::make_shared<BridgeConfig>(c, dynamic_lib_);
  conf->newGoPluginConfig();
  config_ = conf;
}

void TcpConnPool::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                              Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  Network::Connection& latched_conn = conn_data->connection();
  auto upstream = std::make_unique<HttpTcpBridge>(&callbacks_->upstreamToDownstream(),
                                                  std::move(conn_data), dynamic_lib_, config_);

  ENVOY_LOG(debug, "golang http-tcp bridge get host info: {}", host->cluster().name());

  callbacks_->onPoolReady(std::move(upstream), host, latched_conn.connectionInfoProvider(),
                          latched_conn.streamInfo(), {});
}

HttpTcpBridge::HttpTcpBridge(Router::UpstreamToDownstream* upstream_request,
                             Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream,
                             Dso::HttpTcpBridgeDsoPtr dynamic_lib, BridgeConfigSharedPtr config)
    : encoding_state_(*this), decoding_state_(*this),
      route_entry_(upstream_request->route().routeEntry()), upstream_request_(upstream_request),
      upstream_conn_data_(std::move(upstream)), dynamic_lib_(dynamic_lib) {

  // configId in httpRequest
  configId = config->getConfigId();
  // plugin_name in httpRequest
  plugin_name.data = config->pluginName().data();
  plugin_name.len = config->pluginName().length();

  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);
}

HttpTcpBridge::~HttpTcpBridge() {
  ENVOY_LOG(debug, "golang http-tcp bridge on destroy");

  ASSERT(!decoding_state_.isProcessingInGo() && !encoding_state_.isProcessingInGo());

  dynamic_lib_->envoyGoHttpTcpBridgeOnDestroy(this);
}

void HttpTcpBridge::initResponse() {
  if (decoding_state_.resp_headers == nullptr) {
    auto headers{Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>({})};
    decoding_state_.resp_headers = std::move(headers);
  }
}

Envoy::Http::Status HttpTcpBridge::encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                                 bool end_stream) {
  ENVOY_LOG(debug, "golang http-tcp bridge encodeHeaders, header size: {}, end_stream: {}",
            headers.size(), end_stream);

  // init response headers here since go side may return EndStream status.
  initResponse();

  encoding_state_.req_headers = &headers;
  encoding_state_.setFilterState(FilterState::ProcessingHeader);
  Buffer::OwnedImpl buffer;
  auto s = dynamic_cast<processState*>(&encoding_state_);

  GoUint64 go_status = dynamic_lib_->envoyGoHttpTcpBridgeOnEncodeHeader(
      s, end_stream ? 1 : 0, headers.size(), headers.byteSize(),
      reinterpret_cast<uint64_t>(&buffer), buffer.length());

  encoding_state_.handleHeaderGolangStatus(static_cast<HttpTcpBridgeStatus>(go_status));
  ENVOY_LOG(debug, "golang http-tcp bridge encodeHeaders, state: {}", encoding_state_.stateStr());

  if (encoding_state_.filterState() == FilterState::EndStream) {
    // send resp to downstream, which means terminate the request when error happens in Golang side.
    // TODO(duxin40): use golang to specify status-code when EndStream.
    ENVOY_LOG(warn, "golang http-tcp bridge encodeHeaders, send resp to downstream");

    end_stream = true;
    sendDataToDownstream(buffer, end_stream);
    return Envoy::Http::okStatus();
  }

  // if go side set data for buffer, then we send it to upstream
  bool send_data_to_upstream = (buffer.length() != 0);

  trySendProxyData(send_data_to_upstream, end_stream);

  if (send_data_to_upstream) {
    upstream_conn_data_->connection().write(buffer, upstream_conn_self_half_close_);
  }

  return Envoy::Http::okStatus();
}

void HttpTcpBridge::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "golang http-tcp bridge encodeData, data length: {}, end_stream: {}",
            data.length(), end_stream);

  switch (encoding_state_.filterState()) {
  case FilterState::WaitingData:
    encodeDataGo(data, end_stream);
    break;

  case FilterState::WaitingAllData:
    if (end_stream) {
      if (!encoding_state_.isBufferDataEmpty()) {
        // NP: new data = data_buffer_ + data
        encoding_state_.addBufferData(data);
        data.move(encoding_state_.getBufferData());
      }
      encodeDataGo(data, end_stream);
    } else {
      ENVOY_LOG(debug, "golang http-tcp bridge encodeData, appending data to buffer");
      encoding_state_.addBufferData(data);
    }
    break;

  default:
    PANIC(fmt::format("golang http-tcp bridge encodeData, unexpected state: {}",
                      encoding_state_.stateStr()));
  }
}

// TODO(duxin40): use golang to convert trailers to data buffer.
void HttpTcpBridge::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  Buffer::OwnedImpl data;
  upstream_conn_data_->connection().write(data, true);
}

void HttpTcpBridge::readDisable(bool disable) {
  if (upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    return;
  }
  upstream_conn_data_->connection().readDisable(disable);
}

void HttpTcpBridge::resetStream() {
  upstream_request_ = nullptr;
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void HttpTcpBridge::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "golang http-tcp bridge onUpstreamData, data length: {}, end_stream: {}",
            data.length(), end_stream);

  decoding_state_.setFilterState(FilterState::ProcessingData);
  auto s = dynamic_cast<processState*>(&decoding_state_);

  GoUint64 go_status = dynamic_lib_->envoyGoHttpTcpBridgeOnUpstreamData(
      s, end_stream ? 1 : 0, (*decoding_state_.resp_headers).size(),
      (*decoding_state_.resp_headers).byteSize(), reinterpret_cast<uint64_t>(&data), data.length());
  decoding_state_.setFilterState(FilterState::Done);

  switch (static_cast<HttpTcpBridgeStatus>(go_status)) {
  case HttpTcpBridgeStatus::HttpTcpBridgeEndStream:
    // endStream to downstream which means the whole resp to http has finished.
    //
    // from now on, any further data from upstream tcp conn will not come to onUpstreamData.

    end_stream = true;

  case HttpTcpBridgeStatus::HttpTcpBridgeContinue:
    // go side in onUpstreamData will get each_data_piece, pass data and headers to downstream
    // streaming.
    //
    // NOTICE: when first receive TcpUpstreamContinue, resp headers will be send to http all at
    // once; which means from then on, resp headers will never be sent to http.

    sendDataToDownstream(data, end_stream);
    // clear buffer
    data.drain(data.length());
    break;

  case HttpTcpBridgeStatus::HttpTcpBridgeStopAndBuffer:
    // every data trigger will call go side, and go side get whloe buffered data ever since at every
    // time.
    //
    // if onUpstreamData is called streaming multiple times, data is gradually appended by default,
    // so here do nothing. NOTE: when data is available on a TCP connection,
    // Network::ConnectionImpl::onReadReady() invokes the TLS transport socket via
    // SslSocket::doRead(connection_->read_buffer_), at here the data is appended to read_buffer_,
    // then read_buffer_ is passed to onUpstreamData as data via Network::ReadFilter::onData. refer
    // to:
    // https://www.envoyproxy.io/docs/envoy/latest/intro/life_of_a_request#:~:text=socket%20via%20SslSocket%3A%3A-,doRead,-().%20The%20transport

    if (end_stream) {
      // we will catch this unexpected behaviour from users in Golang side, this should not happens.
      PANIC(fmt::format(
          "golang http-tcp bridge onUpstreamData unexpected go_tatus when end_stream is true: {}",
          int(go_status)));
    }
    break;

  default:
    PANIC(fmt::format("golang http-tcp bridge onUpstreamData, unexpected go_tatus: {}", go_status));
  }
}

void HttpTcpBridge::trySendProxyData(bool send_data_to_upstream, bool end_stream) {
  // headers should only happen once, so use this opportunity to add the proxy header, if
  // configured.
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
      bool self_half_close_connection =
          end_stream && !send_data_to_upstream ? upstream_conn_self_half_close_ : false;
      upstream_conn_data_->connection().write(data, self_half_close_connection);
    }
  }
}

void HttpTcpBridge::encodeDataGo(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(
      debug,
      "golang http-tcp bridge encodeDataGo, passing data to golang, state: {}, end_stream: {}",
      encoding_state_.stateStr(), end_stream);

  encoding_state_.processData();

  auto s = dynamic_cast<processState*>(&encoding_state_);
  GoUint64 go_status = dynamic_lib_->envoyGoHttpTcpBridgeOnEncodeData(
      s, end_stream ? 1 : 0, reinterpret_cast<uint64_t>(&data), data.length());

  encoding_state_.handleDataGolangStatus(static_cast<HttpTcpBridgeStatus>(go_status), end_stream);
  ENVOY_LOG(debug, "golang http-tcp bridge encodeDataGo, state: {}", encoding_state_.stateStr());

  if (encoding_state_.filterState() == FilterState::EndStream) {
    // send resp to downstream, which means terminate the request when error happens in Golang side.
    // TODO(duxin40): use golang to specify status-code when EndStream.
    ENVOY_LOG(warn, "golang http-tcp bridge encodeDataGo, send resp to downstream");

    end_stream = true;
    sendDataToDownstream(data, end_stream);
    return;
  }

  if (encoding_state_.filterState() == FilterState::Done ||
      encoding_state_.filterState() == FilterState::WaitingData) {
    upstream_conn_data_->connection().write(data, upstream_conn_self_half_close_);
  }
}

void HttpTcpBridge::sendDataToDownstream(Buffer::Instance& data, bool end_stream) {
  // we can send resp headers only one time.
  if (!already_send_resp_headers_) {
    ENVOY_LOG(debug, "golang http-tcp bridge send resp headers to downstream. end_stream: {}",
              end_stream);
    // if go side not set status, c++ side set default status
    if (!decoding_state_.resp_headers->Status()) {
      decoding_state_.resp_headers->setStatus(static_cast<uint64_t>(HttpStatusCode::Success));
    }
    upstream_request_->decodeHeaders(std::move(decoding_state_.resp_headers), false);
    already_send_resp_headers_ = true;
  } else {
    ENVOY_LOG(debug,
              "golang http-tcp bridge already_send_resp_headers, we can send resp headers only one "
              "time, so ignore this time. end_stream: {}",
              end_stream);
  }

  upstream_request_->decodeData(data, end_stream);
}

void HttpTcpBridge::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected && upstream_request_) {
    upstream_request_->onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
  }
}

void HttpTcpBridge::onAboveWriteBufferHighWatermark() {
  ENVOY_LOG(debug, "golang http-tcp bridge onAboveWriteBufferHighWatermark");
  if (upstream_request_) {
    upstream_request_->onAboveWriteBufferHighWatermark();
  }
}

void HttpTcpBridge::onBelowWriteBufferLowWatermark() {
  ENVOY_LOG(debug, "golang http-tcp bridge onBelowWriteBufferLowWatermark");
  if (upstream_request_) {
    upstream_request_->onBelowWriteBufferLowWatermark();
  }
}

void copyHeaderMapToGo(const Envoy::Http::HeaderMap& m, GoString* go_strs, char* go_buf) {
  auto i = 0;
  m.iterate([&i, &go_strs,
             &go_buf](const Envoy::Http::HeaderEntry& header) -> Envoy::Http::HeaderMap::Iterate {
    // It's safe to use StringView here, since we will copy them into Golang.
    auto key = header.key().getStringView();
    auto value = header.value().getStringView();

    auto len = key.length();
    // go_strs is the heap memory of go, and the length is twice the number of headers. So range it
    // is safe.
    go_strs[i].n = len;
    go_strs[i].p = go_buf;
    // go_buf is the heap memory of go, and the length is the total length of all keys and values in
    // the header. So use memcpy is safe.
    memcpy(go_buf, key.data(), len); // NOLINT(safe-memcpy)
    go_buf += len;
    i++;

    len = value.length();
    go_strs[i].n = len;
    // go_buf may be an invalid pointer in Golang side when len is 0.
    if (len > 0) {
      go_strs[i].p = go_buf;
      memcpy(go_buf, value.data(), len); // NOLINT(safe-memcpy)
      go_buf += len;
    }
    i++;
    return Envoy::Http::HeaderMap::Iterate::Continue;
  });
}

CAPIStatus HttpTcpBridge::copyHeaders(ProcessorState& state, GoString* go_strs, char* go_buf) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang http-tcp bridge copyHeaders is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  if (auto* encoding_state = dynamic_cast<EncodingProcessorState*>(&state)) {
    copyHeaderMapToGo(*encoding_state->req_headers, go_strs, go_buf);
  } else if (auto* decoding_state = dynamic_cast<DecodingProcessorState*>(&state)) {
    if (already_send_resp_headers_) {
      ENVOY_LOG(error,
                "golang http-tcp bridge invoking cgo api copyHeaders while "
                "already_send_resp_headers_ : {}",
                __func__);
      return CAPIStatus::CAPIInvalidPhase;
    }
    copyHeaderMapToGo(*decoding_state->resp_headers, go_strs, go_buf);
  } else {
    PANIC(fmt::format("unexpected process state type: {}", std::type_index(typeid(state)).name()));
  }

  return CAPIStatus::CAPIOK;
}

CAPIStatus HttpTcpBridge::setRespHeader(ProcessorState& state, absl::string_view key,
                                        absl::string_view value, headerAction act) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang http-tcp bridge setRespHeader is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (already_send_resp_headers_) {
    ENVOY_LOG(error,
              "golang http-tcp bridge invoking cgo api setRespHeader while "
              "already_send_resp_headers_ : {}",
              __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  auto* s = dynamic_cast<DecodingProcessorState*>(&state);
  if (s == nullptr) {
    ENVOY_LOG(debug,
              "golang http-tcp bridge invoking cgo api setRespHeader at invalid state: {} when "
              "dynamic_cast state",
              __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }

  switch (act) {
  case HeaderAdd:
    s->resp_headers->addCopy(Envoy::Http::LowerCaseString(key), value);
    break;

  case HeaderSet:
    s->resp_headers->setCopy(Envoy::Http::LowerCaseString(key), value);
    break;

  default:
    PANIC(fmt::format("unknown header action: {}", int(act)));
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus HttpTcpBridge::removeRespHeader(ProcessorState& state, absl::string_view key) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang http-tcp bridge removeRespHeader is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (already_send_resp_headers_) {
    ENVOY_LOG(error,
              "golang http-tcp bridge invoking cgo api removeRespHeader while "
              "already_send_resp_headers_ : {}",
              __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  auto* s = dynamic_cast<DecodingProcessorState*>(&state);
  if (s == nullptr) {
    ENVOY_LOG(debug,
              "golang http-tcp bridge invoking cgo api removeRespHeader at invalid state: {} when "
              "dynamic_cast state",
              __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }

  s->resp_headers->remove(Envoy::Http::LowerCaseString(key));

  return CAPIStatus::CAPIOK;
}

CAPIStatus HttpTcpBridge::copyBuffer(ProcessorState& state, Buffer::Instance* buffer, char* data) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang http-tcp bridge copyBuffer is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  for (const Buffer::RawSlice& slice : buffer->getRawSlices()) {
    // data is the heap memory of go, and the length is the total length of buffer. So use memcpy is
    // safe.
    memcpy(data, static_cast<const char*>(slice.mem_), slice.len_); // NOLINT(safe-memcpy)
    data += slice.len_;
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus HttpTcpBridge::drainBuffer(ProcessorState& state, Buffer::Instance* buffer,
                                      uint64_t length) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang http-tcp bridge drainBuffer is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  buffer->drain(length);
  return CAPIStatus::CAPIOK;
}

CAPIStatus HttpTcpBridge::setBufferHelper(ProcessorState& state, Buffer::Instance* buffer,
                                          absl::string_view& value, bufferAction action) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang http-tcp bridge setBufferHelper is not processing Go");
    return CAPIStatus::CAPINotInGo;
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

CAPIStatus HttpTcpBridge::getStringValue(int id, uint64_t* value_data, int* value_len) {
  // refer the string to str_value_, not deep clone, make sure it won't be freed while reading
  // it on the Go side.
  switch (static_cast<EnvoyValue>(id)) {
  case EnvoyValue::RouteName:
    str_value_ = upstream_request_->route().virtualHost().routeConfig().name();
    break;
  case EnvoyValue::ClusterName: {
    str_value_ = route_entry_->clusterName();
    break;
  }
  default:
    PANIC(fmt::format("golang http-tcp bridge getStringValue invalid string value id: {}", id));
  }

  *value_data = reinterpret_cast<uint64_t>(str_value_.data());
  *value_len = str_value_.length();
  return CAPIStatus::CAPIOK;
}

CAPIStatus HttpTcpBridge::setSelfHalfCloseForUpstreamConn(int enabled) {
  if (enabled == 1) {
    upstream_conn_self_half_close_ = true;
  } else {
    upstream_conn_self_half_close_ = false;
  }
  return CAPIStatus::CAPIOK;
}

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
