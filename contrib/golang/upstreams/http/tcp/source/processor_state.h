#pragma once

#include <deque>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/http/codes.h"
#include "source/common/http/utility.h"

#include "absl/status/status.h"
#include "contrib/golang/common/dso/dso.h"
// #include "upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

class TcpUpstream;

class Filter;

class BufferList : public NonCopyable {
public:
  BufferList() = default;

  bool empty() const { return bytes_ == 0; }
  // return a new buffer instance, it will existing until moveOut or drain.
  Buffer::Instance& push(Buffer::Instance& data);
  // move all buffer into data, the list is empty then.
  void moveOut(Buffer::Instance& data);
  // clear the latest push in buffer.
  void clearLatest();
  // clear all.
  void clearAll();
  // check the buffer instance if existing
  bool checkExisting(Buffer::Instance* data);

private:
  std::deque<Buffer::InstancePtr> queue_;
  // The total size of buffers in the list.
  uint32_t bytes_{0};
};

// This describes the processor state.
enum class FilterState {
  // Processing header in Go
  ProcessingHeader,
  // Processing data in Go
  ProcessingData,
  // All done
  Done,
};

class ProcessorState : public processState, public Logger::Loggable<Logger::Id::http>, NonCopyable {
public:
  explicit ProcessorState(Filter& filter, httpRequest* r) : filter_(filter) {
    req = r;
  }
  virtual ~ProcessorState() = default;


  FilterState filterState() const { return static_cast<FilterState>(state); }
  void setFilterState(FilterState st) { state = static_cast<int>(st); }
  bool isProcessingInGo() {
    return filterState() == FilterState::ProcessingHeader || filterState() == FilterState::ProcessingData;
  }

  /* data buffer */
  // get state buffer
  Buffer::Instance& getBufferData() { return *data_buffer_.get(); };
  bool isBufferDataEmpty() { return data_buffer_ == nullptr || data_buffer_->length() == 0; };
  void drainBufferData();

  const Envoy::Http::RequestOrResponseHeaderMap* headers{nullptr};
  BufferList doDataList;

protected:
  Buffer::InstancePtr data_buffer_{nullptr};

protected:
  Filter& filter_;
};

class DecodingProcessorState : public ProcessorState {
public:
  explicit DecodingProcessorState(Filter& filter, httpRequest* r) : ProcessorState(filter, r) {
    is_encoding = 0;
  }
  // store response header for http
  Envoy::Http::RequestOrResponseHeaderMap* resp_headers{nullptr};
};

class EncodingProcessorState : public ProcessorState {
public:
  explicit EncodingProcessorState(Filter& filter, httpRequest* r) : ProcessorState(filter, r) {
    is_encoding = 1;
  }
};

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
