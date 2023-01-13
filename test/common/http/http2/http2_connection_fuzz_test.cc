#include <alloca.h>

#include <string>
#include <vector>

#include "source/common/http/http2/codec_impl.h"

#include "test/common/http/http2/http2_connection.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/string_view.h"
#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

static const uint32_t MAX_HEADERS = 32;
static const size_t MAX_HD_TABLE_BUF_SIZE = 4096;

class Http2Frame {
public:
  static constexpr char Preamble[25] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
  static constexpr size_t HeaderSize = 9;
  Http2Frame(uint32_t stream_id, uint8_t type, uint8_t flags) : stream_id_(stream_id) {
    data_.assign(HeaderSize, 0);
    setType(type);
    setFlags(flags);
  }

  Http2Frame(absl::string_view contents) : stream_id_(1) {
    appendData(contents);
    if (data_.size() >= HeaderSize) {
      std::memcpy(&stream_id_, &data_[5], sizeof(stream_id_));
    }
  }

  void setPayloadSize(uint32_t size) {
    data_[0] = (size >> 16) & 0xff;
    data_[1] = (size >> 8) & 0xff;
    data_[2] = size & 0xff;
  }

  void adjustPayloadSize() { setPayloadSize(data_.size() - HeaderSize); }

  void setType(uint8_t type) { data_[3] = type; }
  void setFlags(uint8_t flags) { data_[4] = flags; }

  void makeClientStreamId() {
    uint32_t id = (stream_id_ << 1) + 1;
    if (data_.size() >= HeaderSize) {
      std::memcpy(&data_[5], &id, sizeof(id));
    }
  }

  void makeServerStreamId() {
    uint32_t id = stream_id_ << 1;
    if (data_.size() >= HeaderSize) {
      std::memcpy(&data_[5], &id, sizeof(id));
    }
  }

  void appendData(absl::string_view data) { data_.insert(data_.end(), data.begin(), data.end()); }

  const absl::string_view payload() const {
    const char* d = reinterpret_cast<const char*>(data_.data());
    return absl::string_view(d, data_.size());
  }

private:
  uint32_t stream_id_;
  std::vector<uint8_t> data_;
};

void deflate_headers(nghttp2_hd_deflater* deflater, const test::fuzz::Headers& fragment,
                     std::string& payload) {
#define MAX_HDRS 32
  nghttp2_nv h2hdrs[MAX_HEADERS];
  uint8_t buf[MAX_HD_TABLE_BUF_SIZE];

  unsigned n_headers = fragment.headers().size();
  if (n_headers > MAX_HEADERS)
    n_headers = MAX_HEADERS;
  auto headers = fragment.headers();
  for (unsigned i = 0; i < n_headers; i++) {
    auto hv = headers.at(i);
    // Because I didn't manage to discard const from the char pointer,
    // so that I can assign it to nghttp2_nv fields, copy to stack first.
    size_t keylen = hv.key().size();
    uint8_t* key = reinterpret_cast<uint8_t*>(alloca(keylen));
    std::memcpy(key, hv.key().c_str(), keylen);

    size_t valuelen = hv.value().size();
    uint8_t* value = reinterpret_cast<uint8_t*>(alloca(valuelen));
    std::memcpy(value, hv.value().c_str(), valuelen);

    h2hdrs[i].name = key;
    h2hdrs[i].namelen = keylen;
    h2hdrs[i].value = value;
    h2hdrs[i].valuelen = valuelen;
    h2hdrs[i].flags = 0;
  }
  ssize_t len = nghttp2_hd_deflate_hd(deflater, buf, sizeof(buf), h2hdrs, n_headers);
  if (len > 0)
    payload.append(reinterpret_cast<char*>(buf), len);
}

Http2Frame pb_to_h2_frame(nghttp2_hd_deflater* deflater,
                          const test::common::http::http2::Http2FrameOrJunk& pb_frame) {
  if (pb_frame.has_h2frame()) {
    // Need to encode headers
    auto h2frame = pb_frame.h2frame();
    uint32_t streamid = h2frame.streamid();
    uint8_t flags = static_cast<uint8_t>(h2frame.flags() & 0xff);
    uint8_t type;
    std::string payload;
    if (h2frame.has_data()) {
      type = 0;
      auto f = h2frame.data();
      uint8_t padding_len = static_cast<uint8_t>(f.padding().size() & 0xff);
      payload.push_back(static_cast<char>(padding_len));
      payload.append(f.data());
      payload.append(f.padding().c_str(), padding_len);
    } else if (h2frame.has_headers()) {
      type = 1;
      auto f = h2frame.headers();
      uint8_t padding_len = static_cast<uint8_t>(f.padding().size() & 0xff);
      payload.push_back(static_cast<char>(padding_len));
      uint32_t stream_dependency = f.stream_dependency();
      payload.append(reinterpret_cast<char*>(&stream_dependency), sizeof(stream_dependency));
      deflate_headers(deflater, f.headers(), payload);
      payload.append(f.padding().c_str(), padding_len);
    } else if (h2frame.has_priority()) {
      type = 2;
      auto f = h2frame.priority();
      uint32_t stream_dependency = f.stream_dependency();
      payload.append(reinterpret_cast<char*>(&stream_dependency), sizeof(stream_dependency));
      payload.push_back(static_cast<char>(f.weight() & 0xff));
    } else if (h2frame.has_rst()) {
      type = 3;
      auto f = h2frame.rst();
      uint32_t error_code = f.error_code();
      payload.append(reinterpret_cast<char*>(&error_code), sizeof(error_code));
    } else if (h2frame.has_settings()) {
      type = 4;
      auto f = h2frame.settings();
      for (auto& setting : f.settings()) {
        uint16_t id = static_cast<uint16_t>(setting.identifier() & 0xffff);
        uint32_t value = setting.value();
        payload.append(reinterpret_cast<char*>(&id), sizeof(id));
        payload.append(reinterpret_cast<char*>(&value), sizeof(value));
      }
    } else if (h2frame.has_push_promise()) {
      type = 5;
      auto f = h2frame.push_promise();
      uint8_t padding_len = static_cast<uint8_t>(f.padding().size() & 0xff);
      payload.push_back(static_cast<char>(padding_len));
      uint32_t stream_id = f.stream_id();
      payload.append(reinterpret_cast<char*>(&stream_id), sizeof(stream_id));
      deflate_headers(deflater, f.headers(), payload);
      payload.append(f.padding().c_str(), padding_len);
    } else if (h2frame.has_ping()) {
      type = 6;
      auto f = h2frame.ping();
      uint64_t pdata = f.data();
      payload.append(reinterpret_cast<char*>(&pdata), sizeof(pdata));
    } else if (h2frame.has_go_away()) {
      type = 7;
      auto f = h2frame.go_away();
      uint32_t last_stream_id = f.last_stream_id();
      payload.append(reinterpret_cast<char*>(&last_stream_id), sizeof(last_stream_id));
      uint32_t error_code = f.error_code();
      payload.append(reinterpret_cast<char*>(&error_code), sizeof(error_code));
      payload.append(f.debug_data());
    } else if (h2frame.has_window_update()) {
      type = 8;
      auto f = h2frame.window_update();
      uint32_t wsi = f.window_size_increment();
      payload.append(reinterpret_cast<char*>(&wsi), sizeof(wsi));
    } else if (h2frame.has_continuation()) {
      type = 9;
      auto f = h2frame.continuation();
      deflate_headers(deflater, f.headers(), payload);
    } else {
      // Empty frame
      return Http2Frame("");
    }

    Http2Frame result(streamid, type, flags);
    result.appendData(payload);
    result.adjustPayloadSize();
    return result;
  } else if (pb_frame.has_junk()) {
    // Junk frame
    auto junk = pb_frame.junk();
    absl::string_view contents = junk.data();
    return Http2Frame(contents);
  } else {
    // Empty frame
    return Http2Frame("");
  }
}

envoy::config::core::v3::Http2ProtocolOptions http2Settings() {
  envoy::config::core::v3::Http2ProtocolOptions options(
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions()));

  options.mutable_hpack_table_size()->set_value(MAX_HD_TABLE_BUF_SIZE);
  options.set_allow_metadata(true);
  return options;
}

class Http2Harness {
public:
  Http2Harness() : server_settings(http2Settings()), client_settings(http2Settings()) {
    ON_CALL(mock_server_callbacks, newStream(_, _))
        .WillByDefault(Invoke(
            [&](ResponseEncoder&, bool) -> RequestDecoder& { return orphan_request_decoder; }));
  }

  void fuzz_response(std::vector<Http2Frame>& frames) {
    client_ = std::make_unique<Http2::ClientConnectionImpl>(
        mock_client_connection, mock_client_callbacks,
        Http2::CodecStats::atomicGet(http2_stats, stats_store), random, client_settings,
        Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
        Http2::ProdNghttp2SessionFactory::get());

    Buffer::OwnedImpl init;
    Http2Frame emptySettingsFrame(0, 4, 0);
    init.add(emptySettingsFrame.payload());
    Status status = client_->dispatch(init);
    for (auto frame : frames) {
      Buffer::OwnedImpl framebuf;
      framebuf.add(frame.payload());
      Status status = client_->dispatch(framebuf);
    }
  }
  void fuzz_request(std::vector<Http2Frame>& frames, bool use_oghttp2) {
    TestScopedRuntime scoped_runtime;
    if (use_oghttp2) {
      scoped_runtime.mergeValues({{"envoy.reloadable_features.http2_use_oghttp2", "true"}});
    } else {
      scoped_runtime.mergeValues({{"envoy.reloadable_features.http2_use_oghttp2", "false"}});
    }
    server_ = std::make_unique<Http2::ServerConnectionImpl>(
        mock_server_connection, mock_server_callbacks,
        Http2::CodecStats::atomicGet(http2_stats, stats_store), random, server_settings,
        Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
        envoy::config::core::v3::HttpProtocolOptions::ALLOW);

    Buffer::OwnedImpl payload;
    payload.add(Http2Frame::Preamble, 24);
    static Http2Frame emptySettingsFrame(0, 4, 0);
    payload.add(emptySettingsFrame.payload());
    for (auto frame : frames)
      payload.add(frame.payload());
    Status status = server_->dispatch(payload);
  }

private:
  const envoy::config::core::v3::Http2ProtocolOptions server_settings, client_settings;
  Stats::IsolatedStoreImpl stats_store;
  Http2::CodecStats::AtomicPtr http2_stats;

  NiceMock<MockConnectionCallbacks> mock_client_callbacks;
  NiceMock<Network::MockConnection> mock_client_connection;
  ClientConnectionPtr client_;

  NiceMock<MockRequestDecoder> orphan_request_decoder;
  NiceMock<Network::MockConnection> mock_server_connection;
  NiceMock<MockServerConnectionCallbacks> mock_server_callbacks;
  NiceMock<Random::MockRandomGenerator> random;

  ServerConnectionPtr server_;
};

static std::unique_ptr<Http2Harness> harness;
static void reset_harness() { harness = nullptr; }

// HTTP/2 fuzzer
//
// Uses a mix of structured HTTP/2 frames and junk frames for coverage

DEFINE_PROTO_FUZZER(const test::common::http::http2::Http2ConnectionFuzzCase& input) {
  if (harness == nullptr) {
    harness = std::make_unique<Http2Harness>();
    atexit(reset_harness);
  }

  // Convert input to Http2Frame wire format
  size_t n_frames = input.frames_size();
  if (n_frames == 0)
    return;
  std::vector<Http2Frame> frames;
  nghttp2_hd_deflater* deflater = NULL;
  if (nghttp2_hd_deflate_new(&deflater, MAX_HD_TABLE_BUF_SIZE) != 0)
    return;
  for (auto& pbframe : input.frames()) {
    Http2Frame frame = pb_to_h2_frame(deflater, pbframe);
    frames.push_back(frame);
  }
  nghttp2_hd_deflate_del(deflater);

  harness->fuzz_request(frames, true /* use oghttp2 rather than nghttp2 */);
  harness->fuzz_request(frames, false);
}

} // namespace
} // namespace Http2
} // namespace Http
} // namespace Envoy
