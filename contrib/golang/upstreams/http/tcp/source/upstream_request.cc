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
    dso_lib_->envoyGoTcpUpstreamDestroyPluginConfig(config_id_);
  }
}

void FilterConfig::newGoPluginConfig() {
  ENVOY_LOG(debug, "tcp upstream initializing golang http1-tcp bridge config");
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
    :route_entry_(upstream_request->route().routeEntry()), upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)), dynamic_lib_(dynamic_lib), config_(config) {
  
  // configId from httpRequest
  configId = config->getConfigId();
  // plugin_name from httpRequest
  plugin_name.data = config->pluginName().data();
  plugin_name.len = config->pluginName().length();

  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);
}

TcpUpstream::~TcpUpstream() {
  ENVOY_LOG(debug, "tcp upstream on destroy");
  ASSERT(!isProcessingInGo());

  dynamic_lib_->envoyGoOnTcpUpstreamDestroy(this);
}

void TcpUpstream::initResponse() {
  if (resp_headers_ == nullptr) {
    auto headers{Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>({})};
    resp_headers_ = std::move(headers);
  }
}

Envoy::Http::Status TcpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap& headers,
                                               bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream encodeHeaders, header size: {}, end_stream: {}", headers.size(), end_stream);

  req_headers_ = &headers;
  Buffer::OwnedImpl buffer;
  // auto s = dynamic_cast<processState*>(state);
  setFilterState(FilterState::ProcessingHeader);

  GoUint64 go_status = dynamic_lib_->envoyGoEncodeHeader(
    this, end_stream ? 1 : 0, headers.size(), headers.byteSize(), reinterpret_cast<uint64_t>(&buffer), buffer.length());

  handleHeaderGolangStatus(static_cast<TcpUpstreamStatus>(go_status));
  ENVOY_LOG(debug, "tcp upstream encodeHeaders, state: {}", stateStr());

  bool send_data_to_upstream = (filterState() == FilterState::Done);

  trySendProxyData(send_data_to_upstream, end_stream);

  if (send_data_to_upstream) {
    // when go side decide to send data to upstream in encodeHeaders, here will directly send and encodeData will not send.
    upstream_conn_data_->connection().write(buffer, upstream_conn_self_half_close_);
  }

  return Envoy::Http::okStatus();
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream encodeData, data length: {}, end_stream: {}", data.length(), end_stream);

  switch (filterState()) {
  case FilterState::WaitingData:
    encodeDataGo(data, end_stream);
    break;

  case FilterState::WaitingAllData:
    if (end_stream) {
      if (!isBufferDataEmpty()) {
        // NP: new data = data_buffer_ + data
        addBufferData(data);
        data.move(getBufferData());
      }
      encodeDataGo(data, end_stream);
    } else {
      ENVOY_LOG(debug, "tcp upstream encodeData, appending data to buffer");
      addBufferData(data);
    }
    break;

  case FilterState::Done:
    ENVOY_LOG(debug, "tcp upstream encodeData, already send data to upstream");
    return;

  default:
    ENVOY_LOG(error, "tcp upstream encodeData, unexpected state: {}", stateStr());
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

  GoUint64 go_status = dynamic_lib_->envoyGoOnUpstreamData(
    this, end_stream ? 1 : 0, (*resp_headers_).size(), (*resp_headers_).byteSize(), reinterpret_cast<uint64_t>(&data), data.length());

  switch (static_cast<TcpUpstreamStatus>(go_status)) {
  case TcpUpstreamStatus::TcpUpstreamContinue:
    sendDataToDownstream(data, end_stream);
    data.drain(data.length());
    break;

  case TcpUpstreamStatus::TcpUpstreamStopAndBuffer:
    if (end_stream) {
        ENVOY_LOG(error, "tcp upstream onUpstreamData unexpected go_tatus when end_stream is true: {}", int(go_status));
        PANIC("unreachable");
    }
    // if onUpstreamData is called multiple times, data is gradually appended by default, so here do nothing.
    break;

  case TcpUpstreamStatus::TcpUpstreamSendData:
    end_stream = true;
    sendDataToDownstream(data, end_stream);
    break;

  default:
    ENVOY_LOG(error, "tcp upstream onUpstreamData, unexpected go_tatus: {}", go_status);
    PANIC("unreachable");
    break;
  }
}

void TcpUpstream::trySendProxyData(bool send_data_to_upstream, bool end_stream) {
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
      bool self_half_close_connection = end_stream && !send_data_to_upstream ? upstream_conn_self_half_close_ : false;
      upstream_conn_data_->connection().write(data, self_half_close_connection);
    }
  }
}

void TcpUpstream::encodeDataGo(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream encodeDataGo, passing data to golang, state: {}, end_stream: {}", stateStr(), end_stream);

  setFilterState(FilterState::ProcessingData);

  GoUint64 go_status = dynamic_lib_->envoyGoEncodeData(
    this, end_stream ? 1 : 0, reinterpret_cast<uint64_t>(&data), data.length());
    
  handleDataGolangStatus(static_cast<TcpUpstreamStatus>(go_status), end_stream);

  if (filterState() == FilterState::Done || filterState() == FilterState::WaitingData) {
    upstream_conn_data_->connection().write(data, upstream_conn_self_half_close_);
  }
}

void TcpUpstream::sendDataToDownstream(Buffer::Instance& data, bool end_stream) {
  if (!already_send_resp_headers_) {
    // we can send resp headers only one time.
    if (!resp_headers_->Status()) {
      // if go side not set status, c++ side set default status
      resp_headers_->setStatus(static_cast<uint64_t>(HttpStatusCode::Success));
    }
    upstream_request_->decodeHeaders(std::move(resp_headers_), false);
    already_send_resp_headers_ = true;
  } else {
    ENVOY_LOG(debug, "tcp upstream sendDataToDownstream, already_send_resp_headers, we can send resp headers only one time, so ignore. end_stream: {}", end_stream);
  }
  upstream_request_->decodeData(data, end_stream);
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

CAPIStatus TcpUpstream::copyHeaders(GoString* go_strs, char* go_buf) {
  copyHeaderMapToGo(*req_headers_, go_strs, go_buf);
  return CAPIStatus::CAPIOK;
}

// It won't take affect immidiately while it's invoked from a Go thread, instead, it will post a
// callback to run in the envoy worker thread.
CAPIStatus TcpUpstream::setRespHeader(absl::string_view key, absl::string_view value, headerAction act) {                          
  switch (act) {
  case HeaderAdd:
    resp_headers_->addCopy(Envoy::Http::LowerCaseString(key), value);
    break;

  case HeaderSet:
    resp_headers_->setCopy(Envoy::Http::LowerCaseString(key), value);
    break;

  default:
    RELEASE_ASSERT(false, absl::StrCat("unknown header action: ", act));
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus TcpUpstream::copyBuffer(Buffer::Instance* buffer, char* data) {
  for (const Buffer::RawSlice& slice : buffer->getRawSlices()) {
    // data is the heap memory of go, and the length is the total length of buffer. So use memcpy is
    // safe.
    memcpy(data, static_cast<const char*>(slice.mem_), slice.len_); // NOLINT(safe-memcpy)
    data += slice.len_;
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus TcpUpstream::drainBuffer(Buffer::Instance* buffer, uint64_t length) {
  buffer->drain(length);
  return CAPIStatus::CAPIOK;
}

CAPIStatus TcpUpstream::setBufferHelper(Buffer::Instance* buffer,
                                   absl::string_view& value, bufferAction action) {                 
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

CAPIStatus TcpUpstream::setSelfHalfCloseForUpstreamConn(int enabled) {                                   
  if (enabled == 1) {
    upstream_conn_self_half_close_ = true;
  } else {
    upstream_conn_self_half_close_ = false;
  }
  return CAPIStatus::CAPIOK;
}

std::string state2Str(FilterState state) {
  switch (state) {
  case FilterState::WaitingHeader:
    return "WaitingHeader";
  case FilterState::ProcessingHeader:
    return "ProcessingHeader";
  case FilterState::WaitingData:
    return "WaitingData";
  case FilterState::WaitingAllData:
    return "WaitingAllData";
  case FilterState::ProcessingData:
    return "ProcessingData";
  case FilterState::Done:
    return "Done";
  default:
    return "unknown(" + std::to_string(static_cast<int>(state)) + ")";
  }
}

std::string TcpUpstream::stateStr() {
  auto state_str = state2Str(filterState());
  return state_str;
}

void TcpUpstream::drainBufferData() {
  if (data_buffer_ != nullptr) {
    auto len = data_buffer_->length();
    if (len > 0) {
      ENVOY_LOG(debug, "tcp upstream drain buffer data");
      data_buffer_->drain(len);
    }
  }
}

// headers_ should set to nullptr when return true.
void TcpUpstream::handleHeaderGolangStatus(TcpUpstreamStatus status) {
  ENVOY_LOG(debug, "tcp upstream handleHeaderGolangStatus handle header status, state: {}, status: {}", stateStr(), int(status));
  switch (status) {
  case TcpUpstreamStatus::TcpUpstreamContinue:
    setFilterState(FilterState::WaitingData);
    break;
  
  case TcpUpstreamStatus::TcpUpstreamStopAndBuffer:
    setFilterState(FilterState::WaitingAllData);
    break;

  case TcpUpstreamStatus::TcpUpstreamSendData:
    setFilterState(FilterState::Done);
    break;

  default:
    ENVOY_LOG(error, "tcp upstream handleHeaderGolangStatus unexpected go_tatus: {}", int(status));
    PANIC("unreachable");
    break;
  }

  ENVOY_LOG(debug, "tcp upstream handleHeaderGolangStatus after handle header status, state: {}, status: {}", stateStr(), int(status));
};

void TcpUpstream::handleDataGolangStatus(const TcpUpstreamStatus status, bool end_stream) {
  ENVOY_LOG(debug, "tcp upstream handleDataGolangStatus handle data status, state: {}, status: {}", stateStr(), int(status));
  switch (status) {
    case TcpUpstreamStatus::TcpUpstreamContinue:
      if (end_stream) {
        setFilterState(FilterState::Done);
        break;
      }
      drainBufferData();
      setFilterState(FilterState::WaitingData);
      break;

    case TcpUpstreamStatus::TcpUpstreamStopAndBuffer:
      if (end_stream) {
        ENVOY_LOG(error, "tcp upstream handleDataGolangStatus unexpected go_tatus when end_stream is true: {}", int(status));
        PANIC("unreachable");
        break;
      }
      setFilterState(FilterState::WaitingAllData);
      break;

    default:
      ENVOY_LOG(error, "tcp upstream handleDataGolangStatus unexpected go_tatus: {}", int(status));
      PANIC("unreachable");
      break;
    }

    ENVOY_LOG(debug, "tcp upstream handleDataGolangStatus handle data status, state: {}, status: {}", stateStr(), int(status));
};

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
