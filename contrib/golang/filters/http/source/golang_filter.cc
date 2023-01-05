#include "contrib/golang/filters/http/source/golang_filter.h"

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/grpc/status.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/codec_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

void Filter::onHeadersModified() {
  // Any changes to request headers can affect how the request is going to be
  // routed. If we are changing the headers we also need to clear the route
  // cache.
  decoding_state_.getFilterCallbacks()->downstreamCallbacks()->clearRouteCache();
}

Http::LocalErrorStatus Filter::onLocalReply(const LocalReplyData& data) {
  auto& state = getProcessorState();
  ASSERT(state.isThreadSafe());
  ENVOY_LOG(debug, "golang filter onLocalReply, state: {}, phase: {}, code: {}", state.stateStr(),
            state.phaseStr(), int(data.code_));

  return Http::LocalErrorStatus::Continue;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  ProcessorState& state = decoding_state_;

  ENVOY_LOG(debug, "golang filter decodeHeaders, state: {}, phase: {}, end_stream: {}",
            state.stateStr(), state.phaseStr(), end_stream);

  state.setEndStream(end_stream);

  bool done = doHeaders(state, headers, end_stream);

  return done ? Http::FilterHeadersStatus::Continue : Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  ProcessorState& state = decoding_state_;
  ENVOY_LOG(debug,
            "golang filter decodeData, state: {}, phase: {}, data length: {}, end_stream: {}",
            state.stateStr(), state.phaseStr(), data.length(), end_stream);

  state.setEndStream(end_stream);

  bool done = doData(state, data, end_stream);

  if (done) {
    state.doDataList.moveOut(data);
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  ProcessorState& state = decoding_state_;
  ENVOY_LOG(debug, "golang filter decodeTrailers, state: {}, phase: {}", state.stateStr(),
            state.phaseStr());

  state.setSeenTrailers();

  bool done = doTrailer(state, trailers);

  return done ? Http::FilterTrailersStatus::Continue : Http::FilterTrailersStatus::StopIteration;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  ProcessorState& state = getProcessorState();
  ENVOY_LOG(debug, "golang filter encodeHeaders, state: {}, phase: {}, end_stream: {}",
            state.stateStr(), state.phaseStr(), end_stream);

  encoding_state_.setEndStream(end_stream);

  // NP: may enter encodeHeaders in any phase & any state_,
  // since other filters or filtermanager could call encodeHeaders or sendLocalReply in any time.
  // eg. filtermanager may invoke sendLocalReply, when scheme is invalid,
  // with "Sending local reply with details // http1.invalid_scheme" details.
  if (state.state() != FilterState::Done) {
    ENVOY_LOG(debug,
              "golang filter enter encodeHeaders early, maybe sendLocalReply or encodeHeaders "
              "happened, current state: {}, phase: {}",
              state.stateStr(), state.phaseStr());

    ENVOY_LOG(debug, "golang filter drain data buffer since enter encodeHeaders early");
    // NP: is safe to overwrite it since go code won't read it directly
    // need drain buffer to enable read when it's high watermark
    state.drainBufferData();

    // get the state before changing it.
    bool in_go = state.isProcessingInGo();

    if (in_go) {
      // NP: wait go returns to avoid concurrency conflict in go side.
      local_reply_waiting_go_ = true;
      ENVOY_LOG(debug, "waiting go returns before handle the local reply from other filter");

      // NP: save to another local_headers_ variable to avoid conflict,
      // since the headers_ may be used in Go side.
      local_headers_ = &headers;

      // can not use "StopAllIterationAndWatermark" here, since Go decodeHeaders may return
      // stopAndBuffer, that means it need data buffer and not continue header.
      return Http::FilterHeadersStatus::StopIteration;

    } else {
      ENVOY_LOG(debug, "golang filter clear do data buffer before continue encodeHeader, "
                       "since no go code is running");
      state.doDataList.clearAll();
    }
  }

  enter_encoding_ = true;

  bool done = doHeaders(encoding_state_, headers, end_stream);

  return done ? Http::FilterHeadersStatus::Continue : Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  ProcessorState& state = getProcessorState();
  ENVOY_LOG(debug,
            "golang filter encodeData, state: {}, phase: {}, data length: {}, end_stream: {}",
            state.stateStr(), state.phaseStr(), data.length(), end_stream);

  encoding_state_.setEndStream(end_stream);

  if (local_reply_waiting_go_) {
    ENVOY_LOG(debug, "golang filter appending data to buffer");
    encoding_state_.addBufferData(data);
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  bool done = doData(encoding_state_, data, end_stream);

  if (done) {
    state.doDataList.moveOut(data);
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  ProcessorState& state = getProcessorState();
  ENVOY_LOG(debug, "golang filter encodeTrailers, state: {}, phase: {}", state.stateStr(),
            state.phaseStr());

  encoding_state_.setSeenTrailers();

  if (local_reply_waiting_go_) {
    // NP: save to another local_trailers_ variable to avoid conflict,
    // since the trailers_ may be used in Go side.
    local_trailers_ = &trailers;
    return Http::FilterTrailersStatus::StopIteration;
  }

  bool done = doTrailer(encoding_state_, trailers);

  return done ? Http::FilterTrailersStatus::Continue : Http::FilterTrailersStatus::StopIteration;
}

void Filter::onDestroy() {
  ENVOY_LOG(debug, "golang filter on destroy");

  {
    Thread::LockGuard lock(mutex_);
    if (has_destroyed_) {
      ENVOY_LOG(debug, "golang filter has been destroyed");
      return;
    }
    has_destroyed_ = true;
  }

  ASSERT(req_ != nullptr);
  auto& state = getProcessorState();
  auto reason = state.isProcessingInGo() ? DestroyReason::Terminate : DestroyReason::Normal;

  dynamic_lib_->envoyGoFilterOnHttpDestroy(req_, int(reason));
}

// access_log is executed before the log of the stream filter
void Filter::log(const Http::RequestHeaderMap*, const Http::ResponseHeaderMap*,
                 const Http::ResponseTrailerMap*, const StreamInfo::StreamInfo&) {
  // Todo log phase of stream filter
}

/*** common APIs for filter, both decode and encode ***/

GolangStatus Filter::doHeadersGo(ProcessorState& state, Http::RequestOrResponseHeaderMap& headers,
                                 bool end_stream) {
  ENVOY_LOG(debug, "golang filter passing data to golang, state: {}, phase: {}, end_stream: {}",
            state.stateStr(), state.phaseStr(), end_stream);

  if (req_ == nullptr) {
    // req is used by go, so need to use raw memory and then it is safe to release at the gc
    // finalize phase of the go object.
    req_ = new httpRequestInternal(weak_from_this());
    req_->configId = getMergedConfigId(state);
    req_->plugin_name.data = config_->pluginName().data();
    req_->plugin_name.len = config_->pluginName().length();
  }

  req_->phase = static_cast<int>(state.phase());
  {
    Thread::LockGuard lock(mutex_);
    headers_ = &headers;
  }
  auto status = dynamic_lib_->envoyGoFilterOnHttpHeader(req_, end_stream ? 1 : 0, headers.size(),
                                                        headers.byteSize());
  return static_cast<GolangStatus>(status);
}

bool Filter::doHeaders(ProcessorState& state, Http::RequestOrResponseHeaderMap& headers,
                       bool end_stream) {
  ENVOY_LOG(debug, "golang filter doHeaders, state: {}, phase: {}, end_stream: {}",
            state.stateStr(), state.phaseStr(), end_stream);

  ASSERT(state.isBufferDataEmpty());

  state.processHeader(end_stream);
  auto status = doHeadersGo(state, headers, end_stream);
  auto done = state.handleHeaderGolangStatus(status);
  if (done) {
    Thread::LockGuard lock(mutex_);
    headers_ = nullptr;
  }
  return done;
}

bool Filter::doDataGo(ProcessorState& state, Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "golang filter passing data to golang, state: {}, phase: {}, end_stream: {}",
            state.stateStr(), state.phaseStr(), end_stream);

  state.processData(end_stream);

  Buffer::Instance& buffer = state.doDataList.push(data);

  ASSERT(req_ != nullptr);
  req_->phase = static_cast<int>(state.phase());
  auto status = dynamic_lib_->envoyGoFilterOnHttpData(
      req_, end_stream ? 1 : 0, reinterpret_cast<uint64_t>(&buffer), buffer.length());

  return state.handleDataGolangStatus(static_cast<GolangStatus>(status));
}

bool Filter::doData(ProcessorState& state, Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "golang filter doData, state: {}, phase: {}, end_stream: {}", state.stateStr(),
            state.phaseStr(), end_stream);

  bool done = false;
  switch (state.state()) {
  case FilterState::WaitingData:
    done = doDataGo(state, data, end_stream);
    break;
  case FilterState::WaitingAllData:
    if (end_stream) {
      if (!state.isBufferDataEmpty()) {
        // NP: new data = data_buffer_ + data
        state.addBufferData(data);
        data.move(state.getBufferData());
      }
      // check state again since data_buffer may be full and sendLocalReply with 413.
      // TODO: better not trigger 413 here.
      if (state.state() == FilterState::WaitingAllData) {
        done = doDataGo(state, data, end_stream);
      }
      break;
    }
    // NP: not break, continue
    [[fallthrough]];
  case FilterState::ProcessingHeader:
  case FilterState::ProcessingData:
    ENVOY_LOG(debug, "golang filter appending data to buffer");
    state.addBufferData(data);
    break;
  default:
    ENVOY_LOG(error, "unexpected state: {}", state.stateStr());
    // TODO: terminate stream?
    break;
  }

  ENVOY_LOG(debug, "golang filter doData, return: {}", done);

  return done;
}

bool Filter::doTrailerGo(ProcessorState& state, Http::HeaderMap& trailers) {
  ENVOY_LOG(debug, "golang filter passing trailers to golang, state: {}, phase: {}",
            state.stateStr(), state.phaseStr());

  state.processTrailer();

  ASSERT(req_ != nullptr);
  req_->phase = static_cast<int>(state.phase());
  auto status =
      dynamic_lib_->envoyGoFilterOnHttpHeader(req_, 1, trailers.size(), trailers.byteSize());

  return state.handleTrailerGolangStatus(static_cast<GolangStatus>(status));
}

bool Filter::doTrailer(ProcessorState& state, Http::HeaderMap& trailers) {
  ENVOY_LOG(debug, "golang filter doTrailer, state: {}, phase: {}", state.stateStr(),
            state.phaseStr());

  ASSERT(!state.getEndStream() && !state.isProcessingEndStream());

  {
    Thread::LockGuard lock(mutex_);
    trailers_ = &trailers;
  }

  bool done = false;
  switch (state.state()) {
  case FilterState::WaitingTrailer:
    done = doTrailerGo(state, trailers);
    break;
  case FilterState::WaitingData:
    done = doTrailerGo(state, trailers);
    break;
  case FilterState::WaitingAllData:
    ENVOY_LOG(debug, "golang filter data buffer is empty: {}", state.isBufferDataEmpty());
    // do data first
    if (!state.isBufferDataEmpty()) {
      done = doDataGo(state, state.getBufferData(), false);
    }
    // NP: can not use done as condition here, since done will be false
    // maybe we can remove the done variable totally? by using state_ only?
    // continue trailers
    if (state.state() == FilterState::WaitingTrailer) {
      done = doTrailerGo(state, trailers);
    }
    break;
  case FilterState::ProcessingHeader:
  case FilterState::ProcessingData:
    // do nothing, wait previous task
    break;
  default:
    ENVOY_LOG(error, "unexpected state: {}", state.stateStr());
    // TODO: terminate stream?
    break;
  }

  ENVOY_LOG(debug, "golang filter doTrailer, return: {}", done);

  return done;
}

/*** APIs for go call C ***/

void Filter::continueEncodeLocalReply(ProcessorState& state) {
  ENVOY_LOG(debug,
            "golang filter continue encodeHeader(local reply from other filters) after return from "
            "go, current state: {}, phase: {}",
            state.stateStr(), state.phaseStr());

  ENVOY_LOG(debug, "golang filter drain do data buffer before continueEncodeLocalReply");
  state.doDataList.clearAll();

  local_reply_waiting_go_ = false;
  // should use encoding_state_ now
  enter_encoding_ = true;

  auto header_end_stream = encoding_state_.getEndStream();
  if (local_trailers_ != nullptr) {
    Thread::LockGuard lock(mutex_);
    trailers_ = local_trailers_;
    header_end_stream = false;
  }
  if (!encoding_state_.isBufferDataEmpty()) {
    header_end_stream = false;
  }
  // NP: we not overwrite state end_stream in doHeadersGo
  encoding_state_.processHeader(header_end_stream);
  auto status = doHeadersGo(encoding_state_, *local_headers_, header_end_stream);
  continueStatusInternal(status);
}

void Filter::continueStatusInternal(GolangStatus status) {
  ProcessorState& state = getProcessorState();
  ASSERT(state.isThreadSafe());
  auto saved_state = state.state();

  if (local_reply_waiting_go_) {
    ENVOY_LOG(debug,
              "other filter already trigger sendLocalReply, ignoring the continue status: {}, "
              "state: {}, phase: {}",
              int(status), state.stateStr(), state.phaseStr());

    continueEncodeLocalReply(state);
    return;
  }

  auto done = state.handleGolangStatus(status);
  if (done) {
    switch (saved_state) {
    case FilterState::ProcessingHeader:
      // NP: should process data first filter seen the stream is end but go doesn't,
      // otherwise, the next filter will continue with end_stream = true.

      // NP: it is safe to continueDoData after continueProcessing
      // that means injectDecodedDataToFilterChain after continueDecoding while stream is not end
      if (state.isProcessingEndStream() || !state.isStreamEnd()) {
        state.continueProcessing();
      }
      break;

    case FilterState::ProcessingData:
      state.continueDoData();
      break;

    case FilterState::ProcessingTrailer:
      state.continueDoData();
      state.continueProcessing();
      break;

    default:
      ASSERT(0, "unexpected state");
    }
  }

  // TODO: state should also grow in this case
  // state == WaitingData && bufferData is empty && seen trailers

  auto current_state = state.state();
  if ((current_state == FilterState::WaitingData &&
       (!state.isBufferDataEmpty() || state.getEndStream())) ||
      (current_state == FilterState::WaitingAllData && state.isStreamEnd())) {
    auto done = doDataGo(state, state.getBufferData(), state.getEndStream());
    if (done) {
      state.continueDoData();
    } else {
      // do not process trailers when data is not finished
      return;
    }
  }

  Thread::ReleasableLockGuard lock(mutex_);
  if (state.state() == FilterState::WaitingTrailer && trailers_ != nullptr) {
    auto trailers = trailers_;
    lock.release();
    auto done = doTrailerGo(state, *trailers);
    if (done) {
      state.continueProcessing();
    }
  }
}

void Filter::sendLocalReplyInternal(
    Http::Code response_code, absl::string_view body_text,
    std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
    Grpc::Status::GrpcStatus grpc_status, absl::string_view details) {
  ENVOY_LOG(debug, "sendLocalReply Internal, response code: {}", int(response_code));

  ProcessorState& state = getProcessorState();

  if (local_reply_waiting_go_) {
    ENVOY_LOG(debug,
              "other filter already invoked sendLocalReply or encodeHeaders, ignoring the local "
              "reply from go, code: {}, body: {}, details: {}",
              int(response_code), body_text, details);

    continueEncodeLocalReply(state);
    return;
  }

  ENVOY_LOG(debug, "golang filter drain do data buffer before sendLocalReply");
  state.doDataList.clearAll();

  // drain buffer data if it's not empty, before sendLocalReply
  state.drainBufferData();

  state.sendLocalReply(response_code, body_text, modify_headers, grpc_status, details);
}

void Filter::sendLocalReply(Http::Code response_code, absl::string_view body_text,
                            std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                            Grpc::Status::GrpcStatus grpc_status, absl::string_view details) {
  ENVOY_LOG(debug, "sendLocalReply, response code: {}", int(response_code));

  auto& state = getProcessorState();
  auto weak_ptr = weak_from_this();
  state.getDispatcher().post(
      [this, &state, weak_ptr, response_code, body_text, modify_headers, grpc_status, details] {
        ASSERT(state.isThreadSafe());
        // TODO: do not need lock here, since it's the work thread now.
        Thread::ReleasableLockGuard lock(mutex_);
        if (!weak_ptr.expired() && !has_destroyed_) {
          lock.release();
          sendLocalReplyInternal(response_code, body_text, modify_headers, grpc_status, details);
        } else {
          ENVOY_LOG(debug, "golang filter has gone or destroyed in sendLocalReply");
        }
      });
};

void Filter::continueStatus(GolangStatus status) {
  // TODO: skip post event to dispatcher, and return continue in the caller,
  // when it's invoked in the current envoy thread, for better performance & latency.
  auto& state = getProcessorState();
  ENVOY_LOG(debug, "golang filter continue from Go, status: {}, state: {}, phase: {}", int(status),
            state.stateStr(), state.phaseStr());

  auto weak_ptr = weak_from_this();
  state.getDispatcher().post([this, &state, weak_ptr, status] {
    ASSERT(state.isThreadSafe());
    // TODO: do not need lock here, since it's the work thread now.
    Thread::ReleasableLockGuard lock(mutex_);
    if (!weak_ptr.expired() && !has_destroyed_) {
      lock.release();
      continueStatusInternal(status);
    } else {
      ENVOY_LOG(debug, "golang filter has gone or destroyed in continueStatus event");
    }
  });
}

absl::optional<absl::string_view> Filter::getHeader(absl::string_view key) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return "";
  }
  auto& state = getProcessorState();
  auto m = state.isProcessingHeader() ? headers_ : trailers_;
  auto result = m->get(Http::LowerCaseString(key));

  if (result.empty()) {
    return absl::nullopt;
  }
  return result[0]->value().getStringView();
}

void copyHeaderMapToGo(Http::HeaderMap& m, GoString* go_strs, char* go_buf) {
  auto i = 0;
  m.iterate([&i, &go_strs, &go_buf](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    auto key = std::string(header.key().getStringView());
    auto value = std::string(header.value().getStringView());

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
    go_strs[i].p = go_buf;
    memcpy(go_buf, value.data(), len); // NOLINT(safe-memcpy)
    go_buf += len;
    i++;
    return Http::HeaderMap::Iterate::Continue;
  });
}

void Filter::copyHeaders(GoString* go_strs, char* go_buf) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return;
  }
  ASSERT(headers_ != nullptr, "headers is empty, may already continue to next filter");
  copyHeaderMapToGo(*headers_, go_strs, go_buf);
}

void Filter::setHeader(absl::string_view key, absl::string_view value) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return;
  }
  ASSERT(headers_ != nullptr, "headers is empty, may already continue to next filter");
  headers_->setCopy(Http::LowerCaseString(key), value);
  onHeadersModified();
}

void Filter::removeHeader(absl::string_view key) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return;
  }
  ASSERT(headers_ != nullptr, "headers is empty, may already continue to next filter");
  headers_->remove(Http::LowerCaseString(key));
  onHeadersModified();
}

void Filter::copyBuffer(Buffer::Instance* buffer, char* data) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return;
  }
  for (const Buffer::RawSlice& slice : buffer->getRawSlices()) {
    // data is the heap memory of go, and the length is the total length of buffer. So use memcpy is
    // safe.
    memcpy(data, static_cast<const char*>(slice.mem_), slice.len_); // NOLINT(safe-memcpy)
    data += slice.len_;
  }
}

void Filter::setBufferHelper(Buffer::Instance* buffer, absl::string_view& value,
                             bufferAction action) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return;
  }
  if (action == bufferAction::Set) {
    buffer->drain(buffer->length());
  } else if (action == bufferAction::Prepend) {
    buffer->prepend(value);
    return;
  }
  buffer->add(value);
}

void Filter::copyTrailers(GoString* go_strs, char* go_buf) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return;
  }
  ASSERT(trailers_ != nullptr, "trailers is empty");
  copyHeaderMapToGo(*trailers_, go_strs, go_buf);
}

void Filter::setTrailer(absl::string_view key, absl::string_view value) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return;
  }
  ASSERT(trailers_ != nullptr, "trailers is empty");
  trailers_->setCopy(Http::LowerCaseString(key), value);
}

void Filter::getStringValue(int id, GoString* value_str) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return;
  }
  auto& state = getProcessorState();
  switch (static_cast<StringValue>(id)) {
  case StringValue::RouteName:
    // string will copy to req->strValue, but not deep copy
    req_->strValue = state.getRouteName();
    break;
  default:
    ASSERT(false, "invalid string value id");
  }

  value_str->p = req_->strValue.data();
  value_str->n = req_->strValue.length();
}

/* ConfigId */

uint64_t Filter::getMergedConfigId(ProcessorState& state) {
  Http::StreamFilterCallbacks* callbacks = state.getFilterCallbacks();

  // get all of the per route config
  std::list<const FilterConfigPerRoute*> route_config_list;
  callbacks->traversePerFilterConfig(
      [&route_config_list](const Router::RouteSpecificFilterConfig& cfg) {
        route_config_list.push_back(dynamic_cast<const FilterConfigPerRoute*>(&cfg));
      });

  ENVOY_LOG(debug, "golang filter route config list length: {}.", route_config_list.size());

  auto id = config_->getConfigId();
  for (auto it : route_config_list) {
    auto route_config = *it;
    id = route_config.getPluginConfigId(id, config_->pluginName(), config_->soId());
  }

  return id;
}

/*** FilterConfig ***/

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config)
    : plugin_name_(proto_config.plugin_name()), so_id_(proto_config.library_id()),
      so_path_(proto_config.library_path()), plugin_config_(proto_config.plugin_config()) {
  ENVOY_LOG(debug, "initilizing golang filter config");
  // NP: dso may not loaded yet, can not invoke envoyGoFilterNewHttpPluginConfig yet.
};

uint64_t FilterConfig::getConfigId() {
  if (config_id_ != 0) {
    return config_id_;
  }
  auto dlib = Dso::DsoInstanceManager::getDsoInstanceByID(so_id_);
  ASSERT(dlib != nullptr, "load at the config parse phase, so it should not be null");

  std::string str;
  ASSERT(plugin_config_.SerializeToString(&str));
  auto ptr = reinterpret_cast<unsigned long long>(str.data());
  auto len = str.length();
  config_id_ = dlib->envoyGoFilterNewHttpPluginConfig(ptr, len);
  ASSERT(config_id_, "config id is always grows");

  return config_id_;
}

FilterConfigPerRoute::FilterConfigPerRoute(
    const envoy::extensions::filters::http::golang::v3alpha::ConfigsPerRoute& config,
    Server::Configuration::ServerFactoryContext&) {
  // NP: dso may not loaded yet, can not invoke envoyGoFilterNewHttpPluginConfig yet.
  ENVOY_LOG(debug, "initilizing per route golang filter config");

  for (const auto& it : config.plugins_config()) {
    auto plugin_name = it.first;
    auto route_plugin = it.second;
    RoutePluginConfigPtr conf(new RoutePluginConfig(route_plugin));
    ENVOY_LOG(debug, "per route golang filter config, type_url: {}",
              route_plugin.config().type_url());
    plugins_config_.insert({plugin_name, std::move(conf)});
  }
}

uint64_t FilterConfigPerRoute::getPluginConfigId(uint64_t parent_id, std::string plugin_name,
                                                 std::string so_id) const {
  auto it = plugins_config_.find(plugin_name);
  if (it != plugins_config_.end()) {
    return it->second->getMergedConfigId(parent_id, so_id);
  }
  ENVOY_LOG(debug, "golang filter not found plugin config: {}", plugin_name);
  // not found
  return parent_id;
}

uint64_t RoutePluginConfig::getMergedConfigId(uint64_t parent_id, std::string so_id) {
  if (merged_config_id_ > 0) {
    return merged_config_id_;
  }

  auto dlib = Dso::DsoInstanceManager::getDsoInstanceByID(so_id);
  ASSERT(dlib != nullptr, "load at the config parse phase, so it should not be null");

  if (config_id_ == 0) {
    std::string str;
    ASSERT(plugin_config_.SerializeToString(&str));
    auto ptr = reinterpret_cast<unsigned long long>(str.data());
    auto len = str.length();
    config_id_ = dlib->envoyGoFilterNewHttpPluginConfig(ptr, len);
    ASSERT(config_id_, "config id is always grows");
    ENVOY_LOG(debug, "golang filter new plugin config, id: {}", config_id_);
  }

  merged_config_id_ = dlib->envoyGoFilterMergeHttpPluginConfig(parent_id, config_id_);
  ASSERT(merged_config_id_, "config id is always grows");
  ENVOY_LOG(debug, "golang filter merge plugin config, from {} + {} to {}", parent_id, config_id_,
            merged_config_id_);
  return merged_config_id_;
};

/* ProcessorState */
ProcessorState& Filter::getProcessorState() {
  return enter_encoding_ ? dynamic_cast<ProcessorState&>(encoding_state_)
                         : dynamic_cast<ProcessorState&>(decoding_state_);
};

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
