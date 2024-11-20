#include "upstream_request.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "envoy/upstream/upstream.h"

#include "processor_state.h"
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

  plugin_name_ptr = name_ptr;
  plugin_name_len = plugin_name_.length();
  config_ptr = buf_ptr;
  config_len = buf.length();

  config_id_ = dso_lib_->envoyGoOnTcpUpstreamConfig(this);

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
    auto s = c.ParseFromString(config.SerializeAsString());
    ASSERT(s, "any.value() ParseFromString should always successful");

    ENVOY_LOG(debug, "tcp upstream load tcp_upstream_golang library at parse config: {} {}", c.library_id(), c.library_path());

    dynamic_lib_ = Dso::DsoManager<Dso::TcpUpstreamDsoImpl>::load(
      c.library_id(), c.library_path(), c.plugin_name());
    if (dynamic_lib_ == nullptr) {
      throw EnvoyException(fmt::format("tcp upstream : load library failed: {} {}", c.library_id(), c.library_path()));
    };

    FilterConfigSharedPtr conf = std::make_shared<FilterConfig>(c, dynamic_lib_);
    conf->newGoPluginConfig();
    config_ = conf;
    plugin_name_ = c.plugin_name();
}

void TcpConnPool::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                              Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  Network::Connection& latched_conn = conn_data->connection();
  auto upstream =
      std::make_unique<TcpUpstream>(&callbacks_->upstreamToDownstream(), std::move(conn_data), dynamic_lib_, config_);

  ENVOY_LOG(debug, "tcp upstream get host info: {}", host->cluster().name());

  callbacks_->onPoolReady(std::move(upstream), host, latched_conn.connectionInfoProvider(),latched_conn.streamInfo(), {});       
}

TcpUpstream::TcpUpstream(Router::UpstreamToDownstream* upstream_request,
                         Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream, Dso::TcpUpstreamDsoPtr dynamic_lib,
                         FilterConfigSharedPtr config)
    :encoding_state_(new EncodingProcessorState(*this)), decoding_state_(new DecodingProcessorState(*this)),
     route_entry_(upstream_request->route().routeEntry()), upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)), dynamic_lib_(dynamic_lib), config_(config) {
  
  configId = config->getConfigId();
  plugin_name.data = config->pluginName().data();
  plugin_name.len = config->pluginName().length();

  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);
}

TcpUpstream::~TcpUpstream() {
  ENVOY_LOG(debug, "tcp upstream on destroy");

  // initRequest haven't be called yet, which mean haven't called into Go.
  if (configId == 0) {
    return;
  }              

  auto reason = isProcessingInGo()
                ? DestroyReason::Terminate
                : DestroyReason::Normal;
  dynamic_lib_->envoyGoOnTcpUpstreamDestroy(this, int(reason));
}

void TcpUpstream::initRequest() {
  if (configId == 0) {
    configId = config_->getConfigId();
  }
};

void TcpUpstream::initResponse() {
  if (resp_headers_ == nullptr) {
    auto headers{Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>({})};
    resp_headers_ = std::move(headers);
  }
}

bool TcpUpstream::isProcessingInGo() {
  return decoding_state_->isProcessingInGo() || encoding_state_->isProcessingInGo();
}  

Envoy::Http::Status TcpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                               bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream encodeHeaders, header size: {}, end_stream: {}", headers.size(), end_stream);

  initRequest();

  ProcessorState* state = encoding_state_;
  state->headers = &headers;
  Buffer::OwnedImpl buf;
  Buffer::Instance& buffer = state->doDataList.push(buf);
  auto s = dynamic_cast<processState*>(state);
  state->setFilterState(FilterState::ProcessingHeader);
  ENVOY_LOG(debug, "tcp upstream encodeHeaders, state: {}", state->stateStr());
  GoUint64 go_tatus = dynamic_lib_->envoyGoEncodeHeader(
    s, end_stream ? 1 : 0, headers.size(), headers.byteSize(), reinterpret_cast<uint64_t>(&buffer), buffer.length());
  state->doDataList.moveOut(buf);

  bool upstream_conn_half_close;
  switch (static_cast<EncodeHeaderStatus>(go_tatus)) {
  case EncodeHeaderStatus::EncodeHeaderSendDataWithNotHalfClose:
    state->setFilterState(FilterState::Done);
    upstream_conn_half_close = false;
    upstream_conn_data_->connection().write(buf, upstream_conn_half_close);
    break;

  case EncodeHeaderStatus::EncodeHeaderSendDataWithHalfClose:
    state->setFilterState(FilterState::Done);
    upstream_conn_half_close = true;
    upstream_conn_data_->connection().write(buf, upstream_conn_half_close);
    break;

  case EncodeHeaderStatus::EncodeHeaderContinue:
    state->setFilterState(FilterState::WaitingData);
    break;
  
  case EncodeHeaderStatus::EncodeHeaderStopAndBuffer:
    state->setFilterState(FilterState::WaitingAllData);
    break;

  default:
    ENVOY_LOG(error, "tcp upstream encodeHeaders, unexpected go_tatus: {}", go_tatus);
    PANIC("unreachable");
    break;
  }

  // Headers should only happen once, so use this opportunity to add the proxy header, if configured.
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
      ENVOY_LOG(error, "tcp upstream encodeHeaders, encodeData send proxy : {}", data.toString());
      upstream_conn_data_->connection().write(data, upstream_conn_half_close);
    }
  }

  if (buf.length() != 0) {
    upstream_conn_data_->connection().write(buf, false);
  }

  return Envoy::Http::okStatus();
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream encodeData, data length: {}, end_stream: {}", data.length(), end_stream);

  ProcessorState* state = encoding_state_;

  switch (state->filterState()) {
  case FilterState::WaitingData:
    encodeDataGo(state, data, end_stream);
    break;

  case FilterState::WaitingAllData:
    if (end_stream) {
      if (!state->isBufferDataEmpty()) {
        // NP: new data = data_buffer_ + data
        state->addBufferData(data);
        data.move(state->getBufferData());
      }
      encodeDataGo(state, data, end_stream);
    } else {
      ENVOY_LOG(debug, "tcp upstream encodeData, appending data to buffer");
      state->addBufferData(data);
    }
    break;

  case FilterState::Done:
    ENVOY_LOG(debug, "tcp upstream encodeData, already send data to upstream");
    return;

  default:
    ENVOY_LOG(error, "tcp upstream encodeData, unexpected state: {}", state->stateStr());
    PANIC("unreachable");
    break;
  }
}

void TcpUpstream::encodeDataGo(ProcessorState* state, Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream encodeDataGo, passing data to golang, state: {}, end_stream: {}", state->stateStr(), end_stream);

  state->processData();

  Buffer::Instance& buffer = state->doDataList.push(data);
  auto s = dynamic_cast<processState*>(state);
  GoUint64 go_tatus = dynamic_lib_->envoyGoEncodeData(
    s, end_stream ? 1 : 0, reinterpret_cast<uint64_t>(&buffer), buffer.length());

  switch (static_cast<EncodeDataStatus>(go_tatus)) {
  case EncodeDataStatus::EncodeDataContinueWithNotHalfClose:
    state->doDataList.moveOut(data);
    state->setFilterState(FilterState::Done);
    upstream_conn_data_->connection().write(data, false);
    break;

  case EncodeDataStatus::EncodeDataContinueWithHalfClose:
    state->doDataList.moveOut(data);
    state->setFilterState(FilterState::Done);
    upstream_conn_data_->connection().write(data, true);
    break;

  case EncodeDataStatus::EncodeDataStopAndBuffer:
    if (end_stream) {
      ENVOY_LOG(error, "tcp upstream encodeDataGo, unexpected go_tatus when end_stream is true: {}", go_tatus);
      PANIC("unreachable");
      break;
    }
    state->setFilterState(FilterState::WaitingAllData);
    break;

  case EncodeDataStatus::EncodeDataStopNoBuffer:
    if (end_stream) {
      ENVOY_LOG(error, "tcp upstream encodeDataGo, unexpected go_tatus when end_stream is true: {}", go_tatus);
      PANIC("unreachable");
      break;
    }
    state->drainBufferData();
    state->doDataList.clearAll();
    state->setFilterState(FilterState::WaitingData);
    break;

  default:
    ENVOY_LOG(error, "tcp upstream encodeDataGo, unexpected go_tatus: {}", go_tatus);
    PANIC("unreachable");
    break;
  }
}

// TODO(duxin40): use golang to convert trailers to data buffer.
void TcpUpstream::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  Buffer::OwnedImpl data;
  upstream_conn_data_->connection().write(data, true);
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
  ENVOY_LOG(debug, "tcp upstream onUpstreamData, data length: {}, end_stream: {}", data.length(), end_stream);

  initResponse();

  DecodingProcessorState* state = decoding_state_;
  state->resp_headers = static_cast<Envoy::Http::RequestOrResponseHeaderMap*>(resp_headers_.get());
  Buffer::Instance& buffer = state->doDataList.push(data);
  auto s = dynamic_cast<processState*>(state);
  state->setFilterState(FilterState::ProcessingData);

  GoUint64 go_tatus = dynamic_lib_->envoyGoOnUpstreamData(
    s, end_stream ? 1 : 0, (*resp_headers_).size(), (*resp_headers_).byteSize(), reinterpret_cast<uint64_t>(&buffer), buffer.length());
  state->doDataList.moveOut(data);
  state->setFilterState(FilterState::Done);

  switch (static_cast<DecodeDataStatus>(go_tatus)) {
  case DecodeDataStatus::DecodeDataContinue:
    end_stream = true;
    if (!resp_headers_->Status()) {
      // if go side not set status, c++ side set default status
      resp_headers_->setStatus(static_cast<uint64_t>(HttpStatusCode::Success));
    }

    upstream_request_->decodeHeaders(std::move(resp_headers_), false);
    upstream_request_->decodeData(data, end_stream);
    break;

  case DecodeDataStatus::DecodeDataStopAndBuffer:
    // before we set end_stream=true, if onUpstreamData is called multiple times, data is gradually appended by default.
    break;

  case DecodeDataStatus::DecodeDataStopNoBuffer:
    data.drain(data.length());
    break;

  default:
    ENVOY_LOG(error, "tcp upstream onUpstreamData, unexpected go_tatus: {}", go_tatus);
    PANIC("unreachable");
    break;
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

void copyHeaderMapToGo(const Envoy::Http::HeaderMap& m, GoString* go_strs, char* go_buf) {
  auto i = 0;
  m.iterate([&i, &go_strs, &go_buf](const Envoy::Http::HeaderEntry& header) -> Envoy::Http::HeaderMap::Iterate {
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

CAPIStatus TcpUpstream::copyHeaders(ProcessorState& state, GoString* go_strs, char* go_buf) {
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "tcp upstream is not processing Go, {}", state.stateStr());
    return CAPIStatus::CAPINotInGo;
  }
  auto headers = state.headers;
  if (headers == nullptr) {
    ENVOY_LOG(debug, "tcp upstream invoking cgo api at invalid state: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  copyHeaderMapToGo(*headers, go_strs, go_buf);
  return CAPIStatus::CAPIOK;
}

// It won't take affect immidiately while it's invoked from a Go thread, instead, it will post a
// callback to run in the envoy worker thread.
CAPIStatus TcpUpstream::setRespHeader(ProcessorState& state, absl::string_view key, absl::string_view value,
                             headerAction act) {                          
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "tcp upstream is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  auto* s = dynamic_cast<DecodingProcessorState*>(&state);
  if (s == nullptr) {
    ENVOY_LOG(debug, "tcp upstream invoking cgo api at invalid state: {} when dynamic_cast state", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }

  auto headers = s->resp_headers;
  if (headers == nullptr) {
    ENVOY_LOG(debug, "tcp upstream invoking cgo api at invalid state: {} when get resp headers", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }

  switch (act) {
  case HeaderAdd:
    headers->addCopy(Envoy::Http::LowerCaseString(key), value);
    break;

  case HeaderSet:
    headers->setCopy(Envoy::Http::LowerCaseString(key), value);
    break;

  default:
    RELEASE_ASSERT(false, absl::StrCat("unknown header action: ", act));
  }
  return CAPIStatus::CAPIOK;
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
  // refer the string to strValue, not deep clone, make sure it won't be freed while reading
  // it on the Go side.
  switch (static_cast<EnvoyValue>(id)) {
  case EnvoyValue::RouteName:
    // strValue = upstream_request_->route().virtualHost().routeConfig().name();
    this->strValue = "a";
    break;
  case EnvoyValue::ClusterName: {
    strValue = route_entry_->clusterName();
    break;
  }
  default:
    RELEASE_ASSERT(false, absl::StrCat("tcp upstream invalid string value id: ", id));
  }

  *value_data = reinterpret_cast<uint64_t>(strValue.data());
  *value_len = strValue.length();
  return CAPIStatus::CAPIOK;
}

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
