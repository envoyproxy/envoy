#include "processor_state.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

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

std::string ProcessorState::stateStr() {
  std::string prefix = is_encoding == 1 ? "encoder" : "decoder";
  auto state_str = state2Str(filterState());
  return prefix + ":" + state_str;
}

void ProcessorState::processData() {
  ASSERT(filterState() == FilterState::WaitingData ||
          (filterState() == FilterState::WaitingAllData));
  setFilterState(FilterState::ProcessingData);
}
void ProcessorState::drainBufferData() {
  if (data_buffer_ != nullptr) {
    auto len = data_buffer_->length();
    if (len > 0) {
      ENVOY_LOG(debug, "golng http1-tcp bridge drain buffer data");
      data_buffer_->drain(len);
    }
  }
}

// headers_ should set to nullptr when return true.
void ProcessorState::handleHeaderGolangStatus(TcpUpstreamStatus status) {
  ENVOY_LOG(debug, "golng http1-tcp bridge handleHeaderGolangStatus handle header status, state: {}, status: {}", stateStr(),
            int(status));

  ASSERT(filterState() == FilterState::ProcessingHeader);

  switch (status) {
  case TcpUpstreamStatus::TcpUpstreamContinue:
    // will go to encodeData, go side in encodeData will streaming get each_data_piece.

    setFilterState(FilterState::WaitingData);
    break;
  
  case TcpUpstreamStatus::TcpUpstreamStopAndBuffer:
    // will go to encodeData, encodeData will buffer whole data, go side in encodeData get whole data one-off.

    setFilterState(FilterState::WaitingAllData);
    break;

  default:
    ENVOY_LOG(error, "golng http1-tcp bridge handleHeaderGolangStatus unexpected go_tatus: {}", int(status));
    PANIC("unreachable");
    break;
  }

  ENVOY_LOG(debug, "golng http1-tcp bridge handleHeaderGolangStatus after handle header status, state: {}, status: {}", stateStr(), int(status));
};

void ProcessorState::handleDataGolangStatus(const TcpUpstreamStatus status, bool end_stream) {
  ENVOY_LOG(debug, "golng http1-tcp bridge handleDataGolangStatus handle data status, state: {}, status: {}", stateStr(),
            int(status));

  ASSERT(filterState() == FilterState::ProcessingData);

  switch (status) {
    case TcpUpstreamStatus::TcpUpstreamContinue:
      // streaming send data to upstream, go side get each_data_piece, may be called multipled times.
      if (end_stream) {
        setFilterState(FilterState::Done);
        break;
      }
      drainBufferData();
      setFilterState(FilterState::WaitingData);
      break;

    case TcpUpstreamStatus::TcpUpstreamStopAndBuffer:
      // buffer further whole data, go side in encodeData get whole data one-off.

      if (end_stream) {
        ENVOY_LOG(error, "golng http1-tcp bridge handleDataGolangStatus unexpected go_tatus when end_stream is true: {}", int(status));
        PANIC("unreachable");
        break;
      }
      setFilterState(FilterState::WaitingAllData);
      break;

    default:
      ENVOY_LOG(error, "golng http1-tcp bridge handleDataGolangStatus unexpected go_tatus: {}", int(status));
      PANIC("unreachable");
      break;
    }

    ENVOY_LOG(debug, "golng http1-tcp bridge handleDataGolangStatus handle data status, state: {}, status: {}", stateStr(), int(status));
};

DecodingProcessorState::DecodingProcessorState(TcpUpstream& tcp_upstream) : ProcessorState(dynamic_cast<httpRequest*>(&tcp_upstream)) {
    is_encoding = 0;
}

EncodingProcessorState::EncodingProcessorState(TcpUpstream& tcp_upstream) : ProcessorState(dynamic_cast<httpRequest*>(&tcp_upstream)) {
    is_encoding = 1;
}

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
