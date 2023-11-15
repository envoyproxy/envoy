#include "contrib/golang/filters/http/source/golang_filter.h"

#include <cstddef>
#include <cstdint>
#include <memory>
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
#include "source/extensions/filters/common/expr/context.h"

#include "eval/public/cel_value.h"
#include "eval/public/containers/field_access.h"
#include "eval/public/containers/field_backed_list_impl.h"
#include "eval/public/containers/field_backed_map_impl.h"
#include "eval/public/structs/cel_proto_wrapper.h"

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

  request_headers_ = &headers;

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

  // do nothing, stream reset may happen before entering this filter.
  if (req_ == nullptr) {
    return;
  }

  {
    Thread::LockGuard lock(mutex_);
    if (has_destroyed_) {
      ENVOY_LOG(debug, "golang filter has been destroyed");
      return;
    }
    has_destroyed_ = true;
  }

  auto& state = getProcessorState();
  auto reason = state.isProcessingInGo() ? DestroyReason::Terminate : DestroyReason::Normal;

  dynamic_lib_->envoyGoFilterOnHttpDestroy(req_, int(reason));
}

// access_log is executed before the log of the stream filter
void Filter::log(const Formatter::HttpFormatterContext& log_context,
                 const StreamInfo::StreamInfo&) {
  // `log` may be called multiple times with different log type
  switch (log_context.accessLogType()) {
  case Envoy::AccessLog::AccessLogType::DownstreamStart:
  case Envoy::AccessLog::AccessLogType::DownstreamPeriodic:
  case Envoy::AccessLog::AccessLogType::DownstreamEnd: {
    auto& state = getProcessorState();

    if (req_ == nullptr) {
      // log called by AccessLogDownstreamStart will happen before doHeaders
      initRequest(state);

      request_headers_ = static_cast<Http::RequestOrResponseHeaderMap*>(
          const_cast<Http::RequestHeaderMap*>(&log_context.requestHeaders()));
    }

    state.enterLog();
    req_->phase = static_cast<int>(state.phase());
    dynamic_lib_->envoyGoFilterOnHttpLog(req_, int(log_context.accessLogType()));
    state.leaveLog();
  } break;
  default:
    // skip calling with unsupported log types
    break;
  }
}

/*** common APIs for filter, both decode and encode ***/

GolangStatus Filter::doHeadersGo(ProcessorState& state, Http::RequestOrResponseHeaderMap& headers,
                                 bool end_stream) {
  ENVOY_LOG(debug, "golang filter passing data to golang, state: {}, phase: {}, end_stream: {}",
            state.stateStr(), state.phaseStr(), end_stream);

  if (req_ == nullptr) {
    initRequest(state);
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
    FALLTHRU;
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
  Buffer::OwnedImpl body;
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
      // NP: can not use done as condition here, since done will be false
      // maybe we can remove the done variable totally? by using state_ only?
      // continue trailers
      if (state.state() == FilterState::WaitingTrailer) {
        state.continueDoData();
        done = doTrailerGo(state, trailers);
      }
    } else {
      state.continueDoData();
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

CAPIStatus
Filter::sendLocalReply(Http::Code response_code, std::string body_text,
                       std::function<void(Http::ResponseHeaderMap& headers)> modify_headers,
                       Grpc::Status::GrpcStatus grpc_status, std::string details) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  ENVOY_LOG(debug, "sendLocalReply, response code: {}", int(response_code));

  auto weak_ptr = weak_from_this();
  state.getDispatcher().post(
      [this, &state, weak_ptr, response_code, body_text, modify_headers, grpc_status, details] {
        if (!weak_ptr.expired() && !hasDestroyed()) {
          ASSERT(state.isThreadSafe());
          sendLocalReplyInternal(response_code, body_text, modify_headers, grpc_status, details);
        } else {
          ENVOY_LOG(debug, "golang filter has gone or destroyed in sendLocalReply");
        }
      });
  return CAPIStatus::CAPIOK;
};

CAPIStatus Filter::sendPanicReply(absl::string_view details) {
  config_->stats().panic_error_.inc();
  ENVOY_LOG(error, "[go_plugin_http][{}] {}", config_->pluginName(),
            absl::StrCat("filter paniced with error details: ", details));
  // We choose not to pass along the details in the response because
  // we don't want to leak the operational details of the service for security reasons.
  // Operators should be able to view the details via the log message above
  // and use the stats for o11y
  return sendLocalReply(Http::Code::InternalServerError, "error happened in filter\r\n", nullptr,
                        Grpc::Status::WellKnownGrpcStatus::Ok, "");
}

CAPIStatus Filter::continueStatus(GolangStatus status) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  ENVOY_LOG(debug, "golang filter continue from Go, status: {}, state: {}, phase: {}", int(status),
            state.stateStr(), state.phaseStr());

  auto weak_ptr = weak_from_this();
  // TODO: skip post event to dispatcher, and return continue in the caller,
  // when it's invoked in the current envoy thread, for better performance & latency.
  state.getDispatcher().post([this, &state, weak_ptr, status] {
    if (!weak_ptr.expired() && !hasDestroyed()) {
      ASSERT(state.isThreadSafe());
      continueStatusInternal(status);
    } else {
      ENVOY_LOG(debug, "golang filter has gone or destroyed in continueStatus event");
    }
  });
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::getHeader(absl::string_view key, uint64_t* value_data, int* value_len) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  auto m = state.isProcessingHeader() ? headers_ : trailers_;
  if (m == nullptr) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  auto result = m->get(Http::LowerCaseString(key));

  if (!result.empty()) {
    auto str = result[0]->value().getStringView();
    *value_data = reinterpret_cast<uint64_t>(str.data());
    *value_len = str.length();
  }
  return CAPIStatus::CAPIOK;
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

CAPIStatus Filter::copyHeaders(GoString* go_strs, char* go_buf) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (headers_ == nullptr) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  copyHeaderMapToGo(*headers_, go_strs, go_buf);
  return CAPIStatus::CAPIOK;
}

// It won't take affect immidiately while it's invoked from a Go thread, instead, it will post a
// callback to run in the envoy worker thread.
CAPIStatus Filter::setHeader(absl::string_view key, absl::string_view value, headerAction act) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (headers_ == nullptr) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }

  if (state.isThreadSafe()) {
    // it's safe to write header in the safe thread.
    switch (act) {
    case HeaderAdd:
      headers_->addCopy(Http::LowerCaseString(key), value);
      break;

    case HeaderSet:
      headers_->setCopy(Http::LowerCaseString(key), value);
      break;

    default:
      RELEASE_ASSERT(false, absl::StrCat("unknown header action: ", act));
    }

    onHeadersModified();
  } else {
    // should deep copy the string_view before post to dipatcher callback.
    auto key_str = std::string(key);
    auto value_str = std::string(value);

    auto weak_ptr = weak_from_this();
    // dispatch a callback to write header in the envoy safe thread, to make the write operation
    // safety. otherwise, there might be race between reading in the envoy worker thread and writing
    // in the Go thread.
    state.getDispatcher().post([this, weak_ptr, key_str, value_str, act] {
      if (!weak_ptr.expired() && !hasDestroyed()) {
        Thread::LockGuard lock(mutex_);
        switch (act) {
        case HeaderAdd:
          headers_->addCopy(Http::LowerCaseString(key_str), value_str);
          break;

        case HeaderSet:
          headers_->setCopy(Http::LowerCaseString(key_str), value_str);
          break;

        default:
          RELEASE_ASSERT(false, absl::StrCat("unknown header action: ", act));
        }

        onHeadersModified();
      } else {
        ENVOY_LOG(debug, "golang filter has gone or destroyed in setHeader");
      }
    });
  }

  return CAPIStatus::CAPIOK;
}

// It won't take affect immidiately while it's invoked from a Go thread, instead, it will post a
// callback to run in the envoy worker thread.
CAPIStatus Filter::removeHeader(absl::string_view key) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (headers_ == nullptr) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  if (state.isThreadSafe()) {
    // it's safe to write header in the safe thread.
    headers_->remove(Http::LowerCaseString(key));
    onHeadersModified();
  } else {
    // should deep copy the string_view before post to dipatcher callback.
    auto key_str = std::string(key);

    auto weak_ptr = weak_from_this();
    // dispatch a callback to write header in the envoy safe thread, to make the write operation
    // safety. otherwise, there might be race between reading in the envoy worker thread and writing
    // in the Go thread.
    state.getDispatcher().post([this, weak_ptr, key_str] {
      if (!weak_ptr.expired() && !hasDestroyed()) {
        Thread::LockGuard lock(mutex_);
        headers_->remove(Http::LowerCaseString(key_str));
        onHeadersModified();
      } else {
        ENVOY_LOG(debug, "golang filter has gone or destroyed in removeHeader");
      }
    });
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::copyBuffer(Buffer::Instance* buffer, char* data) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (!state.doDataList.checkExisting(buffer)) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
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

CAPIStatus Filter::drainBuffer(Buffer::Instance* buffer, uint64_t length) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (!state.doDataList.checkExisting(buffer)) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }

  buffer->drain(length);
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::setBufferHelper(Buffer::Instance* buffer, absl::string_view& value,
                                   bufferAction action) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (!state.doDataList.checkExisting(buffer)) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
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

CAPIStatus Filter::copyTrailers(GoString* go_strs, char* go_buf) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (trailers_ == nullptr) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  copyHeaderMapToGo(*trailers_, go_strs, go_buf);
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::setTrailer(absl::string_view key, absl::string_view value, headerAction act) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (trailers_ == nullptr) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  if (state.isThreadSafe()) {
    switch (act) {
    case HeaderAdd:
      trailers_->addCopy(Http::LowerCaseString(key), value);
      break;

    case HeaderSet:
      trailers_->setCopy(Http::LowerCaseString(key), value);
      break;

    default:
      RELEASE_ASSERT(false, absl::StrCat("unknown header action: ", act));
    }
  } else {
    // should deep copy the string_view before post to dipatcher callback.
    auto key_str = std::string(key);
    auto value_str = std::string(value);

    auto weak_ptr = weak_from_this();
    // dispatch a callback to write trailer in the envoy safe thread, to make the write operation
    // safety. otherwise, there might be race between reading in the envoy worker thread and
    // writing in the Go thread.
    state.getDispatcher().post([this, weak_ptr, key_str, value_str, act] {
      if (!weak_ptr.expired() && !hasDestroyed()) {
        Thread::LockGuard lock(mutex_);
        switch (act) {
        case HeaderAdd:
          trailers_->addCopy(Http::LowerCaseString(key_str), value_str);
          break;

        case HeaderSet:
          trailers_->setCopy(Http::LowerCaseString(key_str), value_str);
          break;

        default:
          RELEASE_ASSERT(false, absl::StrCat("unknown header action: ", act));
        }
      } else {
        ENVOY_LOG(debug, "golang filter has gone or destroyed in setTrailer");
      }
    });
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::removeTrailer(absl::string_view key) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }
  if (trailers_ == nullptr) {
    ENVOY_LOG(debug, "invoking cgo api at invalid phase: {}", __func__);
    return CAPIStatus::CAPIInvalidPhase;
  }
  if (state.isThreadSafe()) {
    trailers_->remove(Http::LowerCaseString(key));
  } else {
    // should deep copy the string_view before post to dipatcher callback.
    auto key_str = std::string(key);

    auto weak_ptr = weak_from_this();
    // dispatch a callback to write trailer in the envoy safe thread, to make the write operation
    // safety. otherwise, there might be race between reading in the envoy worker thread and writing
    // in the Go thread.
    state.getDispatcher().post([this, weak_ptr, key_str] {
      if (!weak_ptr.expired() && !hasDestroyed()) {
        Thread::LockGuard lock(mutex_);
        trailers_->remove(Http::LowerCaseString(key_str));
      } else {
        ENVOY_LOG(debug, "golang filter has gone or destroyed in removeTrailer");
      }
    });
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::getIntegerValue(int id, uint64_t* value) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  switch (static_cast<EnvoyValue>(id)) {
  case EnvoyValue::Protocol:
    if (!state.streamInfo().protocol().has_value()) {
      return CAPIStatus::CAPIValueNotFound;
    }
    *value = static_cast<uint64_t>(state.streamInfo().protocol().value());
    break;
  case EnvoyValue::ResponseCode:
    if (!state.streamInfo().responseCode().has_value()) {
      return CAPIStatus::CAPIValueNotFound;
    }
    *value = state.streamInfo().responseCode().value();
    break;
  case EnvoyValue::AttemptCount:
    if (!state.streamInfo().attemptCount().has_value()) {
      return CAPIStatus::CAPIValueNotFound;
    }
    *value = state.streamInfo().attemptCount().value();
    break;
  default:
    RELEASE_ASSERT(false, absl::StrCat("invalid integer value id: ", id));
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::getStringValue(int id, uint64_t* value_data, int* value_len) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }
  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  // refer the string to req_->strValue, not deep clone, make sure it won't be freed while reading
  // it on the Go side.
  switch (static_cast<EnvoyValue>(id)) {
  case EnvoyValue::RouteName:
    req_->strValue = state.streamInfo().getRouteName();
    break;
  case EnvoyValue::FilterChainName: {
    const auto filter_chain_info = state.streamInfo().downstreamAddressProvider().filterChainInfo();
    req_->strValue =
        filter_chain_info.has_value() ? std::string(filter_chain_info->name()) : std::string();
    break;
  }
  case EnvoyValue::ResponseCodeDetails:
    if (!state.streamInfo().responseCodeDetails().has_value()) {
      return CAPIStatus::CAPIValueNotFound;
    }
    req_->strValue = state.streamInfo().responseCodeDetails().value();
    break;
  case EnvoyValue::DownstreamLocalAddress:
    req_->strValue = state.streamInfo().downstreamAddressProvider().localAddress()->asString();
    break;
  case EnvoyValue::DownstreamRemoteAddress:
    req_->strValue = state.streamInfo().downstreamAddressProvider().remoteAddress()->asString();
    break;
  case EnvoyValue::UpstreamLocalAddress:
    if (state.streamInfo().upstreamInfo() &&
        state.streamInfo().upstreamInfo()->upstreamLocalAddress()) {
      req_->strValue = state.streamInfo().upstreamInfo()->upstreamLocalAddress()->asString();
    } else {
      return CAPIStatus::CAPIValueNotFound;
    }
    break;
  case EnvoyValue::UpstreamRemoteAddress:
    if (state.streamInfo().upstreamInfo() &&
        state.streamInfo().upstreamInfo()->upstreamRemoteAddress()) {
      req_->strValue = state.streamInfo().upstreamInfo()->upstreamRemoteAddress()->asString();
    } else {
      return CAPIStatus::CAPIValueNotFound;
    }
    break;
  case EnvoyValue::UpstreamClusterName:
    if (state.streamInfo().upstreamClusterInfo().has_value() &&
        state.streamInfo().upstreamClusterInfo().value()) {
      req_->strValue = state.streamInfo().upstreamClusterInfo().value()->name();
    } else {
      return CAPIStatus::CAPIValueNotFound;
    }
    break;
  case EnvoyValue::VirtualClusterName:
    if (!state.streamInfo().virtualClusterName().has_value()) {
      return CAPIStatus::CAPIValueNotFound;
    }
    req_->strValue = state.streamInfo().virtualClusterName().value();
    break;
  default:
    RELEASE_ASSERT(false, absl::StrCat("invalid string value id: ", id));
  }

  *value_data = reinterpret_cast<uint64_t>(req_->strValue.data());
  *value_len = req_->strValue.length();
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::getDynamicMetadata(const std::string& filter_name, uint64_t* buf_data,
                                      int* buf_len) {
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }

  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  if (!state.isThreadSafe()) {
    auto weak_ptr = weak_from_this();
    ENVOY_LOG(debug, "golang filter getDynamicMetadata posting request to dispatcher");
    state.getDispatcher().post([this, &state, weak_ptr, filter_name, buf_data, buf_len] {
      ENVOY_LOG(debug, "golang filter getDynamicMetadata request in worker thread");
      if (!weak_ptr.expired() && !hasDestroyed()) {
        populateSliceWithMetadata(state, filter_name, buf_data, buf_len);
        dynamic_lib_->envoyGoRequestSemaDec(req_);
      } else {
        ENVOY_LOG(info, "golang filter has gone or destroyed in getDynamicMetadata");
      }
    });
    return CAPIStatus::CAPIYield;
  } else {
    ENVOY_LOG(debug, "golang filter getDynamicMetadata replying directly");
    populateSliceWithMetadata(state, filter_name, buf_data, buf_len);
  }

  return CAPIStatus::CAPIOK;
}

void Filter::populateSliceWithMetadata(ProcessorState& state, const std::string& filter_name,
                                       uint64_t* buf_data, int* buf_len) {
  const auto& metadata = state.streamInfo().dynamicMetadata().filter_metadata();
  const auto filter_it = metadata.find(filter_name);
  if (filter_it != metadata.end()) {
    filter_it->second.SerializeToString(&req_->strValue);
    *buf_data = reinterpret_cast<uint64_t>(req_->strValue.data());
    *buf_len = req_->strValue.length();
  }
}

CAPIStatus Filter::setDynamicMetadata(std::string filter_name, std::string key,
                                      absl::string_view buf) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }

  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  if (!state.isThreadSafe()) {
    auto weak_ptr = weak_from_this();
    // Since go only waits for the CAPI return code we need to create a deep copy
    // of the buffer slice and pass that to the dispatcher.
    auto buff_copy = std::string(buf);
    state.getDispatcher().post([this, &state, weak_ptr, filter_name, key, buff_copy] {
      if (!weak_ptr.expired() && !hasDestroyed()) {
        ASSERT(state.isThreadSafe());
        setDynamicMetadataInternal(state, filter_name, key, buff_copy);
      } else {
        ENVOY_LOG(info, "golang filter has gone or destroyed in setDynamicMetadata");
      }
    });
    return CAPIStatus::CAPIOK;
  }

  // it's safe to do it here since we are in the safe envoy worker thread now.
  setDynamicMetadataInternal(state, filter_name, key, buf);
  return CAPIStatus::CAPIOK;
}

void Filter::setDynamicMetadataInternal(ProcessorState& state, std::string filter_name,
                                        std::string key, const absl::string_view& buf) {
  ProtobufWkt::Struct value;
  ProtobufWkt::Value v;
  v.ParseFromArray(buf.data(), buf.length());

  (*value.mutable_fields())[key] = v;

  state.streamInfo().setDynamicMetadata(filter_name, value);
}

CAPIStatus Filter::setStringFilterState(absl::string_view key, absl::string_view value,
                                        int state_type, int life_span, int stream_sharing) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }

  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  if (state.isThreadSafe()) {
    state.streamInfo().filterState()->setData(
        key, std::make_shared<GoStringFilterState>(value),
        static_cast<StreamInfo::FilterState::StateType>(state_type),
        static_cast<StreamInfo::FilterState::LifeSpan>(life_span),
        static_cast<StreamInfo::StreamSharingMayImpactPooling>(stream_sharing));
  } else {
    auto key_str = std::string(key);
    auto filter_state = std::make_shared<GoStringFilterState>(value);
    auto weak_ptr = weak_from_this();
    state.getDispatcher().post(
        [this, &state, weak_ptr, key_str, filter_state, state_type, life_span, stream_sharing] {
          if (!weak_ptr.expired() && !hasDestroyed()) {
            Thread::LockGuard lock(mutex_);
            state.streamInfo().filterState()->setData(
                key_str, filter_state, static_cast<StreamInfo::FilterState::StateType>(state_type),
                static_cast<StreamInfo::FilterState::LifeSpan>(life_span),
                static_cast<StreamInfo::StreamSharingMayImpactPooling>(stream_sharing));
          } else {
            ENVOY_LOG(info, "golang filter has gone or destroyed in setStringFilterState");
          }
        });
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::getStringFilterState(absl::string_view key, uint64_t* value_data,
                                        int* value_len) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }

  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  if (state.isThreadSafe()) {
    auto go_filter_state =
        state.streamInfo().filterState()->getDataReadOnly<GoStringFilterState>(key);
    if (go_filter_state) {
      req_->strValue = go_filter_state->value();
      *value_data = reinterpret_cast<uint64_t>(req_->strValue.data());
      *value_len = req_->strValue.length();
    }
  } else {
    auto key_str = std::string(key);
    auto weak_ptr = weak_from_this();
    state.getDispatcher().post([this, &state, weak_ptr, key_str, value_data, value_len] {
      if (!weak_ptr.expired() && !hasDestroyed()) {
        auto go_filter_state =
            state.streamInfo().filterState()->getDataReadOnly<GoStringFilterState>(key_str);
        if (go_filter_state) {
          req_->strValue = go_filter_state->value();
          *value_data = reinterpret_cast<uint64_t>(req_->strValue.data());
          *value_len = req_->strValue.length();
        }
        dynamic_lib_->envoyGoRequestSemaDec(req_);
      } else {
        ENVOY_LOG(info, "golang filter has gone or destroyed in getStringFilterState");
      }
    });
    return CAPIStatus::CAPIYield;
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::getStringProperty(absl::string_view path, uint64_t* value_data, int* value_len,
                                     int* rc) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (has_destroyed_) {
    ENVOY_LOG(debug, "golang filter has been destroyed");
    return CAPIStatus::CAPIFilterIsDestroy;
  }

  auto& state = getProcessorState();
  if (!state.isProcessingInGo()) {
    ENVOY_LOG(debug, "golang filter is not processing Go");
    return CAPIStatus::CAPINotInGo;
  }

  // to access the headers_ and its friends we need to hold the lock
  activation_request_headers_ = dynamic_cast<const Http::RequestHeaderMap*>(request_headers_);
  if (enter_encoding_) {
    activation_response_headers_ = dynamic_cast<const Http::ResponseHeaderMap*>(headers_);
    activation_response_trailers_ = dynamic_cast<const Http::ResponseTrailerMap*>(trailers_);
  }

  if (state.isThreadSafe()) {
    return getStringPropertyCommon(path, value_data, value_len, state);
  }

  auto weak_ptr = weak_from_this();
  state.getDispatcher().post([this, &state, weak_ptr, path, value_data, value_len, rc] {
    if (!weak_ptr.expired() && !hasDestroyed()) {
      *rc = getStringPropertyCommon(path, value_data, value_len, state);
      dynamic_lib_->envoyGoRequestSemaDec(req_);
    } else {
      ENVOY_LOG(info, "golang filter has gone or destroyed in getStringProperty");
    }
  });
  return CAPIStatus::CAPIYield;
}

CAPIStatus Filter::getStringPropertyCommon(absl::string_view path, uint64_t* value_data,
                                           int* value_len, ProcessorState& state) {
  activation_info_ = &state.streamInfo();
  CAPIStatus status = getStringPropertyInternal(path, &req_->strValue);
  if (status == CAPIStatus::CAPIOK) {
    *value_data = reinterpret_cast<uint64_t>(req_->strValue.data());
    *value_len = req_->strValue.length();
  }
  return status;
}

absl::optional<google::api::expr::runtime::CelValue> Filter::findValue(absl::string_view name,
                                                                       Protobuf::Arena* arena) {
  // as we already support getting/setting FilterState, we don't need to implement
  // getProperty with non-attribute name & setProperty which actually work on FilterState
  return StreamActivation::FindValue(name, arena);
  // we don't need to call resetActivation as activation_xx_ is overridden when we get property
}

CAPIStatus Filter::getStringPropertyInternal(absl::string_view path, std::string* result) {
  using google::api::expr::runtime::CelValue;

  bool first = true;
  CelValue value;
  Protobuf::Arena arena;

  size_t start = 0;
  while (true) {
    if (start >= path.size()) {
      break;
    }

    size_t end = path.find('.', start);
    if (end == absl::string_view::npos) {
      end = start + path.size();
    }
    auto part = path.substr(start, end - start);
    start = end + 1;

    if (first) {
      // top-level identifier
      first = false;
      auto top_value = findValue(toAbslStringView(part), &arena);
      if (!top_value.has_value()) {
        return CAPIStatus::CAPIValueNotFound;
      }
      value = top_value.value();
    } else if (value.IsMap()) {
      auto& map = *value.MapOrDie();
      auto field = map[CelValue::CreateStringView(toAbslStringView(part))];
      if (!field.has_value()) {
        return CAPIStatus::CAPIValueNotFound;
      }
      value = field.value();
    } else if (value.IsMessage()) {
      auto msg = value.MessageOrDie();
      if (msg == nullptr) {
        return CAPIStatus::CAPIValueNotFound;
      }
      const Protobuf::Descriptor* desc = msg->GetDescriptor();
      const Protobuf::FieldDescriptor* field_desc = desc->FindFieldByName(std::string(part));
      if (field_desc == nullptr) {
        return CAPIStatus::CAPIValueNotFound;
      }
      if (field_desc->is_map()) {
        value = CelValue::CreateMap(
            Protobuf::Arena::Create<google::api::expr::runtime::FieldBackedMapImpl>(
                &arena, msg, field_desc, &arena));
      } else if (field_desc->is_repeated()) {
        value = CelValue::CreateList(
            Protobuf::Arena::Create<google::api::expr::runtime::FieldBackedListImpl>(
                &arena, msg, field_desc, &arena));
      } else {
        auto status =
            google::api::expr::runtime::CreateValueFromSingleField(msg, field_desc, &arena, &value);
        if (!status.ok()) {
          return CAPIStatus::CAPIInternalFailure;
        }
      }
    } else if (value.IsList()) {
      auto& list = *value.ListOrDie();
      int idx = 0;
      if (!absl::SimpleAtoi(toAbslStringView(part), &idx)) {
        return CAPIStatus::CAPIValueNotFound;
      }
      if (idx < 0 || idx >= list.size()) {
        return CAPIStatus::CAPIValueNotFound;
      }
      value = list[idx];
    } else {
      return CAPIStatus::CAPIValueNotFound;
    }
  }

  return serializeStringValue(value, result);
}

CAPIStatus Filter::serializeStringValue(Filters::Common::Expr::CelValue value,
                                        std::string* result) {
  using Filters::Common::Expr::CelValue;
  const Protobuf::Message* out_message;

  switch (value.type()) {
  case CelValue::Type::kString:
    result->assign(value.StringOrDie().value().data(), value.StringOrDie().value().size());
    return CAPIStatus::CAPIOK;
  case CelValue::Type::kBytes:
    result->assign(value.BytesOrDie().value().data(), value.BytesOrDie().value().size());
    return CAPIStatus::CAPIOK;
  case CelValue::Type::kInt64:
    result->assign(absl::StrCat(value.Int64OrDie()));
    return CAPIStatus::CAPIOK;
  case CelValue::Type::kUint64:
    result->assign(absl::StrCat(value.Uint64OrDie()));
    return CAPIStatus::CAPIOK;
  case CelValue::Type::kDouble:
    result->assign(absl::StrCat(value.DoubleOrDie()));
    return CAPIStatus::CAPIOK;
  case CelValue::Type::kBool:
    result->assign(value.BoolOrDie() ? "true" : "false");
    return CAPIStatus::CAPIOK;
  case CelValue::Type::kDuration:
    result->assign(absl::FormatDuration(value.DurationOrDie()));
    return CAPIStatus::CAPIOK;
  case CelValue::Type::kTimestamp:
    result->assign(absl::FormatTime(value.TimestampOrDie(), absl::UTCTimeZone()));
    return CAPIStatus::CAPIOK;
  case CelValue::Type::kMessage:
    out_message = value.MessageOrDie();
    result->clear();
    if (!out_message || out_message->SerializeToString(result)) {
      return CAPIStatus::CAPIOK;
    }
    return CAPIStatus::CAPISerializationFailure;
  case CelValue::Type::kMap: {
    // so far, only headers/trailers/filter state are in Map format, and we already have API to
    // fetch them
    ENVOY_LOG(error, "map type property result is not supported yet");
    return CAPIStatus::CAPISerializationFailure;
  }
  case CelValue::Type::kList: {
    ENVOY_LOG(error, "list type property result is not supported yet");
    return CAPIStatus::CAPISerializationFailure;
  }
  default:
    return CAPIStatus::CAPISerializationFailure;
  }
}

void Filter::initRequest(ProcessorState& state) {
  // req is used by go, so need to use raw memory and then it is safe to release at the gc
  // finalize phase of the go object.
  req_ = new httpRequestInternal(weak_from_this());
  req_->configId = getMergedConfigId(state);
  req_->plugin_name.data = config_->pluginName().data();
  req_->plugin_name.len = config_->pluginName().length();
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
    id = route_config.getPluginConfigId(id, config_->pluginName());
  }

  return id;
}

/*** FilterConfig ***/

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::golang::v3alpha::Config& proto_config,
    Dso::HttpFilterDsoPtr dso_lib, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context)
    : plugin_name_(proto_config.plugin_name()), so_id_(proto_config.library_id()),
      so_path_(proto_config.library_path()), plugin_config_(proto_config.plugin_config()),
      stats_(GolangFilterStats::generateStats(stats_prefix, context.scope())), dso_lib_(dso_lib),
      metric_store_(std::make_shared<MetricStore>(context.scope().createScope(""))){};

void FilterConfig::newGoPluginConfig() {
  ENVOY_LOG(debug, "initializing golang filter config");
  std::string buf;
  auto res = plugin_config_.SerializeToString(&buf);
  ASSERT(res, "SerializeToString should always successful");
  auto buf_ptr = reinterpret_cast<unsigned long long>(buf.data());
  auto name_ptr = reinterpret_cast<unsigned long long>(plugin_name_.data());

  config_ = new httpConfigInternal(weak_from_this());
  config_->plugin_name_ptr = name_ptr;
  config_->plugin_name_len = plugin_name_.length();
  config_->config_ptr = buf_ptr;
  config_->config_len = buf.length();
  config_->is_route_config = 0;

  config_id_ = dso_lib_->envoyGoFilterNewHttpPluginConfig(config_);

  if (config_id_ == 0) {
    throw EnvoyException(
        fmt::format("golang filter failed to parse plugin config: {} {}", so_id_, so_path_));
  }

  ENVOY_LOG(debug, "golang filter new plugin config, id: {}", config_id_);
}

FilterConfig::~FilterConfig() {
  if (config_id_ > 0) {
    dso_lib_->envoyGoFilterDestroyHttpPluginConfig(config_id_, 0);
  }
}

CAPIStatus FilterConfig::defineMetric(uint32_t metric_type, absl::string_view name,
                                      uint32_t* metric_id) {
  Thread::LockGuard lock(mutex_);
  if (metric_type > static_cast<uint32_t>(MetricType::Max)) {
    return CAPIStatus::CAPIValueNotFound;
  }

  auto type = static_cast<MetricType>(metric_type);

  Stats::StatNameManagedStorage storage(name, metric_store_->scope_->symbolTable());
  Stats::StatName stat_name = storage.statName();
  if (type == MetricType::Counter) {
    auto id = metric_store_->nextCounterMetricId();
    auto c = &metric_store_->scope_->counterFromStatName(stat_name);
    metric_store_->counters_.emplace(id, c);
    *metric_id = id;
  } else if (type == MetricType::Gauge) {
    auto id = metric_store_->nextGaugeMetricId();
    auto g =
        &metric_store_->scope_->gaugeFromStatName(stat_name, Stats::Gauge::ImportMode::Accumulate);
    metric_store_->gauges_.emplace(id, g);
    *metric_id = id;
  } else { // (type == MetricType::Histogram)
    ASSERT(type == MetricType::Histogram);
    auto id = metric_store_->nextHistogramMetricId();
    auto h = &metric_store_->scope_->histogramFromStatName(stat_name,
                                                           Stats::Histogram::Unit::Unspecified);
    metric_store_->histograms_.emplace(id, h);
    *metric_id = id;
  }

  return CAPIStatus::CAPIOK;
}

CAPIStatus FilterConfig::incrementMetric(uint32_t metric_id, int64_t offset) {
  Thread::LockGuard lock(mutex_);
  auto type = static_cast<MetricType>(metric_id & MetricStore::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = metric_store_->counters_.find(metric_id);
    if (it != metric_store_->counters_.end()) {
      if (offset > 0) {
        it->second->add(offset);
      }
    }
  } else if (type == MetricType::Gauge) {
    auto it = metric_store_->gauges_.find(metric_id);
    if (it != metric_store_->gauges_.end()) {
      if (offset > 0) {
        it->second->add(offset);
      } else {
        it->second->sub(-offset);
      }
    }
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus FilterConfig::getMetric(uint32_t metric_id, uint64_t* value) {
  Thread::LockGuard lock(mutex_);
  auto type = static_cast<MetricType>(metric_id & MetricStore::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = metric_store_->counters_.find(metric_id);
    if (it != metric_store_->counters_.end()) {
      *value = it->second->value();
    }
  } else if (type == MetricType::Gauge) {
    auto it = metric_store_->gauges_.find(metric_id);
    if (it != metric_store_->gauges_.end()) {
      *value = it->second->value();
    }
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus FilterConfig::recordMetric(uint32_t metric_id, uint64_t value) {
  Thread::LockGuard lock(mutex_);
  auto type = static_cast<MetricType>(metric_id & MetricStore::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = metric_store_->counters_.find(metric_id);
    if (it != metric_store_->counters_.end()) {
      it->second->add(value);
    }
  } else if (type == MetricType::Gauge) {
    auto it = metric_store_->gauges_.find(metric_id);
    if (it != metric_store_->gauges_.end()) {
      it->second->set(value);
    }
  } else {
    ASSERT(type == MetricType::Histogram);
    auto it = metric_store_->histograms_.find(metric_id);
    if (it != metric_store_->histograms_.end()) {
      it->second->recordValue(value);
    }
  }
  return CAPIStatus::CAPIOK;
}

uint64_t FilterConfig::getConfigId() { return config_id_; }

FilterConfigPerRoute::FilterConfigPerRoute(
    const envoy::extensions::filters::http::golang::v3alpha::ConfigsPerRoute& config,
    Server::Configuration::ServerFactoryContext&) {
  // NP: dso may not loaded yet, can not invoke envoyGoFilterNewHttpPluginConfig yet.
  ENVOY_LOG(debug, "initializing per route golang filter config");

  for (const auto& it : config.plugins_config()) {
    auto plugin_name = it.first;
    auto route_plugin = it.second;
    RoutePluginConfigPtr conf(new RoutePluginConfig(plugin_name, route_plugin));
    ENVOY_LOG(debug, "per route golang filter config, type_url: {}",
              route_plugin.config().type_url());
    plugins_config_.insert({plugin_name, std::move(conf)});
  }
}

uint64_t FilterConfigPerRoute::getPluginConfigId(uint64_t parent_id,
                                                 std::string plugin_name) const {
  auto it = plugins_config_.find(plugin_name);
  if (it != plugins_config_.end()) {
    return it->second->getMergedConfigId(parent_id);
  }
  ENVOY_LOG(debug, "golang filter not found plugin config: {}", plugin_name);
  // not found
  return parent_id;
}

RoutePluginConfig::RoutePluginConfig(
    const std::string plugin_name,
    const envoy::extensions::filters::http::golang::v3alpha::RouterPlugin& config)
    : plugin_name_(plugin_name), plugin_config_(config.config()) {

  ENVOY_LOG(debug, "initializing golang filter route plugin config, plugin_name: {}, type_url: {}",
            plugin_name_, config.config().type_url());

  dso_lib_ = Dso::DsoManager<Dso::HttpFilterDsoImpl>::getDsoByPluginName(plugin_name_);
  if (dso_lib_ == nullptr) {
    // RoutePluginConfig may be created before FilterConfig, so dso_lib_ may be null.
    // i.e. per route config is used in LDS route_config.
    return;
  }

  config_id_ = getConfigId();
  if (config_id_ == 0) {
    throw EnvoyException(
        fmt::format("golang filter failed to parse plugin config: {}", plugin_name_));
  }
  ENVOY_LOG(debug, "golang filter new per route '{}' plugin config, id: {}", plugin_name_,
            config_id_);
};

RoutePluginConfig::~RoutePluginConfig() {
  absl::WriterMutexLock lock(&mutex_);
  if (config_id_ > 0) {
    dso_lib_->envoyGoFilterDestroyHttpPluginConfig(config_id_, 0);
  }
  if (merged_config_id_ > 0 && config_id_ != merged_config_id_) {
    dso_lib_->envoyGoFilterDestroyHttpPluginConfig(merged_config_id_, 0);
  }
}

uint64_t RoutePluginConfig::getConfigId() {
  if (dso_lib_ == nullptr) {
    dso_lib_ = Dso::DsoManager<Dso::HttpFilterDsoImpl>::getDsoByPluginName(plugin_name_);
    ASSERT(dso_lib_ != nullptr, "load at the request time, so it should not be null");
  }

  std::string buf;
  auto res = plugin_config_.SerializeToString(&buf);
  ASSERT(res, "SerializeToString is always successful");
  auto buf_ptr = reinterpret_cast<unsigned long long>(buf.data());
  auto name_ptr = reinterpret_cast<unsigned long long>(plugin_name_.data());

  config_ = new httpConfig();
  config_->plugin_name_ptr = name_ptr;
  config_->plugin_name_len = plugin_name_.length();
  config_->config_ptr = buf_ptr;
  config_->config_len = buf.length();
  config_->is_route_config = 1;
  return dso_lib_->envoyGoFilterNewHttpPluginConfig(config_);
};

uint64_t RoutePluginConfig::getMergedConfigId(uint64_t parent_id) {
  {
    // this is the fast path for most cases.
    absl::ReaderMutexLock lock(&mutex_);
    if (merged_config_id_ > 0 && cached_parent_id_ == parent_id) {
      return merged_config_id_;
    }
  }
  absl::WriterMutexLock lock(&mutex_);
  if (merged_config_id_ > 0) {
    if (cached_parent_id_ == parent_id) {
      return merged_config_id_;
    }
    // upper level config changed, merged_config_id_ is outdated.
    // there is a concurrency race:
    // 1. when A envoy worker thread is using the cached merged_config_id_ and it will call into Go
    //    after some time.
    // 2. while B envoy worker thread may update the merged_config_id_ in getMergedConfigId, that
    //    will delete the id.
    // so, we delay deleting the id in the Go side.
    dso_lib_->envoyGoFilterDestroyHttpPluginConfig(merged_config_id_, 1);
  }

  if (config_id_ == 0) {
    config_id_ = getConfigId();
    RELEASE_ASSERT(config_id_, "TODO: terminate request or passthrough");
  }

  auto name_ptr = reinterpret_cast<unsigned long long>(plugin_name_.data());
  merged_config_id_ = dso_lib_->envoyGoFilterMergeHttpPluginConfig(name_ptr, plugin_name_.length(),
                                                                   parent_id, config_id_);
  ASSERT(merged_config_id_, "config id is always grows");
  ENVOY_LOG(debug, "golang filter merge '{}' plugin config, from {} + {} to {}", plugin_name_,
            parent_id, config_id_, merged_config_id_);

  cached_parent_id_ = parent_id;
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
