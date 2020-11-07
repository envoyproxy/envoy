#include "test/extensions/common/tap/common.h"

#include <fstream>

#include "envoy/data/tap/v3/wrapper.pb.h"

namespace envoy {
namespace data {
namespace tap {
namespace v3 {

std::ostream& operator<<(std::ostream& os, const TraceWrapper& trace) {
  return os << Envoy::MessageUtil::getJsonStringFromMessage(trace, true, false);
}

} // namespace v3
} // namespace tap
} // namespace data
} // namespace envoy

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

std::vector<envoy::data::tap::v3::TraceWrapper> readTracesFromPath(const std::string& path_prefix) {
  // Find the written .pb file and verify it.
  auto files = TestUtility::listFiles(path_prefix, false);
  auto pb_file_name = std::find_if(files.begin(), files.end(), [](const std::string& s) {
    return absl::EndsWith(s, MessageUtil::FileExtensions::get().ProtoBinaryLengthDelimited);
  });
  EXPECT_NE(pb_file_name, files.end());
  return readTracesFromFile(*pb_file_name);
}

std::vector<envoy::data::tap::v3::TraceWrapper> readTracesFromFile(const std::string& file) {
  std::vector<envoy::data::tap::v3::TraceWrapper> traces;
  std::ifstream pb_file(file, std::ios_base::binary);
  Protobuf::io::IstreamInputStream stream(&pb_file);
  Protobuf::io::CodedInputStream coded_stream(&stream);
  while (true) {
    uint32_t message_size;
    if (!coded_stream.ReadVarint32(&message_size)) {
      break;
    }

    traces.emplace_back();

    auto limit = coded_stream.PushLimit(message_size);
    EXPECT_TRUE(traces.back().ParseFromCodedStream(&coded_stream));
    coded_stream.PopLimit(limit);
  }

  return traces;
}

MockPerTapSinkHandleManager::MockPerTapSinkHandleManager() = default;
MockPerTapSinkHandleManager::~MockPerTapSinkHandleManager() = default;

MockMatcher::~MockMatcher() = default;

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
