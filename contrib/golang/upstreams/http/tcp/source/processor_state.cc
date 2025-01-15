#include "processor_state.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/protobuf/utility.h"

#include "upstream_request.h"

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

EncodingProcessorState::EncodingProcessorState(HttpTcpBridge& http_tpc_bridge)
    : ProcessorState(dynamic_cast<httpRequest*>(&http_tpc_bridge)) {
  is_encoding = 1;
}

// headers_ should set to nullptr when return true.
void EncodingProcessorState::handleHeaderGolangStatus(HttpTcpBridgeStatus status) {
  ENVOY_LOG(
      debug,
      "golang http-tcp bridge handleHeaderGolangStatus handle header status, state: {}, status: {}",
      stateStr(), int(status));

  ASSERT(filterState() == FilterState::ProcessingHeader);

  switch (status) {
  case HttpTcpBridgeStatus::HttpTcpBridgeContinue:
    // will go to encodeData, go side in encodeData will streaming get each_data_piece.

    setFilterState(FilterState::WaitingData);
    break;

  case HttpTcpBridgeStatus::HttpTcpBridgeStopAndBuffer:
    // will go to encodeData, encodeData will buffer whole data, go side in encodeData get whole
    // data one-off.

    setFilterState(FilterState::WaitingAllData);
    break;

  case HttpTcpBridgeStatus::HttpTcpBridgeEndStream:
    // will send resp to downstream, which means terminate the request when error happens.

    setFilterState(FilterState::EndStream);
    break;

  default:
    PANIC(fmt::format("golang http-tcp bridge handleHeaderGolangStatus unexpected go_tatus: {}",
                      int(status)));
  }

  ENVOY_LOG(debug,
            "golang http-tcp bridge handleHeaderGolangStatus after handle header status, state: "
            "{}, status: {}",
            stateStr(), int(status));
};

void EncodingProcessorState::handleDataGolangStatus(const HttpTcpBridgeStatus status,
                                                    bool end_stream) {
  ENVOY_LOG(
      debug,
      "golang http-tcp bridge handleDataGolangStatus handle data status, state: {}, status: {}",
      stateStr(), int(status));

  ASSERT(filterState() == FilterState::ProcessingData);

  switch (status) {
  case HttpTcpBridgeStatus::HttpTcpBridgeContinue:
    // streaming send data to upstream, go side get each_data_piece, may be called multipled times.

    if (end_stream) {
      setFilterState(FilterState::Done);
      // http req is end, drain data buffer
      drainBufferData();
      break;
    }

    RELEASE_ASSERT(isBufferDataEmpty(),
                   fmt::format("golang http-tcp bridge handleDataGolangStatus unexpected "
                               "HttpTcpBridgeContinue while data_buffer_ is not empty: {}",
                               int(status)));
    setFilterState(FilterState::WaitingData);
    break;

  case HttpTcpBridgeStatus::HttpTcpBridgeStopAndBuffer:
    // buffer further whole data, go side in encodeData get whole data one-off.

    if (end_stream) {
      // we will catch this unexpected behaviour from users in Golang side, this should not happens.
      PANIC(fmt::format("golang http-tcp bridge handleDataGolangStatus unexpected go_tatus when "
                        "end_stream is true: {}",
                        int(status)));
    }
    setFilterState(FilterState::WaitingAllData);
    break;

  case HttpTcpBridgeStatus::HttpTcpBridgeEndStream:
    // will send resp to downstream, which means terminate the request when error happens.

    setFilterState(FilterState::EndStream);
    break;

  default:
    PANIC(fmt::format("golang http-tcp bridge handleDataGolangStatus unexpected go_tatus: {}",
                      int(status)));
  }

  ENVOY_LOG(
      debug,
      "golang http-tcp bridge handleDataGolangStatus handle data status, state: {}, status: {}",
      stateStr(), int(status));
};

DecodingProcessorState::DecodingProcessorState(HttpTcpBridge& http_tpc_bridge)
    : ProcessorState(dynamic_cast<httpRequest*>(&http_tpc_bridge)) {
  is_encoding = 0;
}

} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
