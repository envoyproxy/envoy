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
  * Used when you want to leave the current func area and continue further func. (when streaming, go side get each_data_piece, may be called multipled times)
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: will go to encodeData, go side in encodeData will streaming get each_data_piece.
  * encodeData: streaming send data to upstream, go side get each_data_piece, may be called multipled times.
  * onUpstreamData: go side in onUpstreamData will get each_data_piece, pass data and headers to downstream streaming.
  */
  TcpUpstreamContinue,

  /** 
  * Area of status: encodeHeaders, encodeData, onUpstreamData
  *
  * Used when you want to buffer data.
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: will go to encodeData, encodeData will buffer whole data, go side in encodeData get whole data one-off.
  * encodeData: buffer further whole data, go side in encodeData get whole data one-off. (Be careful: cannot bed used when end_stream=true)
  * onUpstreamData: every data trigger will call go side, and go side get buffer data from start.
  */
  TcpUpstreamStopAndBuffer,

  /** Area of status: encodeHeaders, onUpstreamData
  *
  * Used when you want to send data to upstream in encodeHeaders, or send data to downstream in onUpstreamData.
  *
  * Here is the specific explanation in different funcs:
  * encodeHeaders: directly send data to upstream, and encodeData will not be called even when downstream_req has body.
  * onUpstreamData: send data and headers to downstream which means the whole resp to http is finished.
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
  void handleDataGolangStatus(const TcpUpstreamStatus status, Buffer::Instance& data,bool end_stream);

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
