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
#include "upstream_request.h"

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

/**
  * This describes the processor state.
*/
enum class FilterState {
  // Waiting header
  WaitingHeader,
  // Processing header in Go
  ProcessingHeader,
  // Waiting data
  WaitingData,
  // Waiting all data
  WaitingAllData,
  // Processing data in Go
  ProcessingData,
  // All done
  Done,
};
/**
  * An enum specific for Golang status.
*/
enum class TcpUpstreamStatus {
  /** 
  * Area of status: encodeHeaders, encodeData, onUpstreamData
  *
  * Used when you want to leave the current func area and continue further func.(when streaming, go side only th current data, not get the rest data of streaming)
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: will go to encodeData.
  * encodeData: send data to upstream.
  * onUpstreamData: pass data and headers to downstream.
  */
  TcpUpstreamContinue,

  /** 
  * Area of status: encodeHeaders, encodeData, onUpstreamData
  *
  * Used when you want to buffer data.
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: will go to encodeData and will buffer whole data(when streaming, only last time will call go side encodeData which means end_stream=true).
  * encodeData: buffer whole data, only last_data_piece will call go side which means end_stream=true.
  * onUpstreamData: buffer whole data, each_data_piece will call go side.
  */
  TcpUpstreamStopAndBuffer,

  /** Area of status: encodeData, onUpstreamData
  *
  * Used when you do not want to buffer data.
  *
  * Here is the specific explanation in different funcs:
  * encodeData: not buffer data, each_data_piece will call go side.
  * onUpstreamData: not buffer data, each_data_piece will call go side.
  */
  TcpUpstreamStopNoBuffer,

  /** Area of status: encodeHeaders
  *
  * Used when you do not want to send data to upstream in encodeHeaders.
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: directly send data to upstream, and encodeData will not be called even when downstream_req has body.
  */
  TcpUpstreamSendData,
};

class ProcessorState : public processState, public Logger::Loggable<Logger::Id::http>, NonCopyable {
public:
  explicit ProcessorState(httpRequest* r) {
    req = r;
    setFilterState(FilterState::WaitingHeader);
  }
  virtual ~ProcessorState() = default;

  void processData();
  std::string stateStr();

  FilterState filterState() const { return static_cast<FilterState>(state); }
  void setFilterState(FilterState st) { state = static_cast<int>(st); }
  bool isProcessingInGo() {
    return filterState() == FilterState::ProcessingHeader || filterState() == FilterState::ProcessingData;
  }

  /* data buffer */
  // add data to state buffer
  virtual void addBufferData(Buffer::Instance& data) {
    if (data_buffer_ == nullptr) {
      data_buffer_ = std::make_unique<Buffer::OwnedImpl>();
    }
    data_buffer_->move(data);
  };
  // get state buffer
  Buffer::Instance& getBufferData() { return *data_buffer_.get(); };
  bool isBufferDataEmpty() { return data_buffer_ == nullptr || data_buffer_->length() == 0; };
  void drainBufferData();

  void handleHeaderGolangStatus(TcpUpstreamStatus status);
  void handleDataGolangStatus(const TcpUpstreamStatus status, bool end_stream);

  const Envoy::Http::RequestOrResponseHeaderMap* headers{nullptr};
  BufferList doDataList;

protected:
  Buffer::InstancePtr data_buffer_{nullptr};

};

class DecodingProcessorState : public ProcessorState {
public:
  DecodingProcessorState(TcpUpstream& tcp_upstream);
  
  // store response header for http
  Envoy::Http::RequestOrResponseHeaderMap* resp_headers{nullptr};
};

class EncodingProcessorState : public ProcessorState {
public:
  EncodingProcessorState(TcpUpstream& tcp_upstream);
};

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
