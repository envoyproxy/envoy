#include <alloca.h>

#include <string>
#include <vector>

#include "source/common/http/http2/codec_impl.h"

#include "test/common/http/http2/http2_connection.pb.h"
#include "test/common/http/http2/http2_frame.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/string_view.h"
#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {
namespace Http2 {
namespace {

static const uint32_t MAX_HEADERS = 32;
static const size_t MAX_HD_TABLE_BUF_SIZE = 4096;

void deflateHeaders(nghttp2_hd_deflater* deflater, const test::fuzz::Headers& fragment,
                    std::string& payload) {
#define MAX_HDRS 32
  nghttp2_nv h2hdrs[MAX_HEADERS];
  uint8_t buf[MAX_HD_TABLE_BUF_SIZE];

  unsigned n_headers = fragment.headers().size();
  if (n_headers > MAX_HEADERS) {
    n_headers = MAX_HEADERS;
  }
  auto headers = fragment.headers();
  for (unsigned i = 0; i < n_headers; i++) {
    auto hv = headers.at(i);
    size_t keylen = hv.key().size();
    uint8_t* key = reinterpret_cast<uint8_t*>(alloca(keylen));
    std::memcpy(key, hv.key().data(), keylen);

    size_t valuelen = hv.value().size();
    uint8_t* value = reinterpret_cast<uint8_t*>(alloca(valuelen));
    std::memcpy(value, hv.value().data(), valuelen);

    h2hdrs[i].name = key;
    h2hdrs[i].namelen = keylen;
    h2hdrs[i].value = value;
    h2hdrs[i].valuelen = valuelen;
    h2hdrs[i].flags = 0;
  }
  ssize_t len = nghttp2_hd_deflate_hd(deflater, buf, sizeof(buf), h2hdrs, n_headers);
  if (len > 0) {
    payload.append(reinterpret_cast<char*>(buf), len);
  }
}

Http2Frame pbToH2Frame(nghttp2_hd_deflater* deflater,
                       const test::common::http::http2::Http2FrameOrJunk& pb_frame) {
  if (pb_frame.has_junk()) {
    // Junk frame
    const auto& junk = pb_frame.junk();
    absl::string_view contents = junk.data();
    return Http2Frame::makeGenericFrame(contents);
  } else if (pb_frame.has_h2frame()) {
    // Need to encode headers
    const auto& h2frame = pb_frame.h2frame();
    uint32_t streamid = Http2Frame::makeClientStreamId(h2frame.streamid());
    uint8_t flags = static_cast<uint8_t>(h2frame.flags() & 0xff);
    bool use_padding = flags & static_cast<uint8_t>(Http2Frame::HeadersFlags::Padded);
    Http2Frame::Type type;
    std::string payload;
    if (h2frame.has_data()) {
      type = Http2Frame::Type::Data;
      const auto& f = h2frame.data();
      payload.append(f.data());
    } else if (h2frame.has_headers()) {
      type = Http2Frame::Type::Headers;
      const auto& f = h2frame.headers();
      uint8_t padding_len = static_cast<uint8_t>(f.padding().size() & 0xff);
      if (use_padding) {
        payload.push_back(static_cast<char>(padding_len));
      }

      uint32_t stream_dependency = htonl(f.stream_dependency());
      payload.append(reinterpret_cast<char*>(&stream_dependency), sizeof(stream_dependency));

      if (f.weight() > 0 && f.weight() <= 256) {
        uint8_t weight = static_cast<uint8_t>((f.weight() - 1) & 0xff);
        payload.append(reinterpret_cast<char*>(&weight), sizeof(weight));
      }

      deflateHeaders(deflater, f.headers(), payload);
      if (use_padding) {
        payload.append(f.padding().data(), padding_len);
      }
    } else if (h2frame.has_priority()) {
      type = Http2Frame::Type::Priority;
      const auto& f = h2frame.priority();
      uint32_t stream_dependency = htonl(f.stream_dependency());
      payload.append(reinterpret_cast<char*>(&stream_dependency), sizeof(stream_dependency));
      payload.push_back(static_cast<char>(f.weight() & 0xff));
    } else if (h2frame.has_rst()) {
      type = Http2Frame::Type::RstStream;
      const auto& f = h2frame.rst();
      uint32_t error_code = htonl(f.error_code());
      payload.append(reinterpret_cast<char*>(&error_code), sizeof(error_code));
    } else if (h2frame.has_settings()) {
      type = Http2Frame::Type::Settings;
      const auto& f = h2frame.settings();
      for (auto& setting : f.settings()) {
        uint16_t id = static_cast<uint16_t>(setting.identifier() & 0xffff);
        uint32_t value = htonl(setting.value());
        payload.append(reinterpret_cast<char*>(&id), sizeof(id));
        payload.append(reinterpret_cast<char*>(&value), sizeof(value));
      }
    } else if (h2frame.has_push_promise()) {
      type = Http2Frame::Type::PushPromise;
      const auto& f = h2frame.push_promise();
      uint8_t padding_len = static_cast<uint8_t>(f.padding().size() & 0xff);
      if (use_padding) {
        payload.push_back(static_cast<char>(padding_len));
      }
      uint32_t stream_id = htonl(f.stream_id());
      payload.append(reinterpret_cast<char*>(&stream_id), sizeof(stream_id));
      deflateHeaders(deflater, f.headers(), payload);
      if (use_padding) {
        payload.append(f.padding().data(), padding_len);
      }
    } else if (h2frame.has_ping()) {
      type = Http2Frame::Type::Ping;
      const auto& f = h2frame.ping();
      uint64_t pdata = f.data();
      payload.append(reinterpret_cast<char*>(&pdata), sizeof(pdata));
    } else if (h2frame.has_go_away()) {
      type = Http2Frame::Type::GoAway;
      const auto& f = h2frame.go_away();
      uint32_t last_stream_id = htonl(f.last_stream_id());
      payload.append(reinterpret_cast<char*>(&last_stream_id), sizeof(last_stream_id));
      uint32_t error_code = htonl(f.error_code());
      payload.append(reinterpret_cast<char*>(&error_code), sizeof(error_code));
      payload.append(f.debug_data());
    } else if (h2frame.has_window_update()) {
      type = Http2Frame::Type::WindowUpdate;
      const auto& f = h2frame.window_update();
      uint32_t wsi = htonl(f.window_size_increment());
      payload.append(reinterpret_cast<char*>(&wsi), sizeof(wsi));
    } else if (h2frame.has_continuation()) {
      type = Http2Frame::Type::Continuation;
      const auto& f = h2frame.continuation();
      deflateHeaders(deflater, f.headers(), payload);
    } else {
      // Empty frame
      return {};
    }
    return Http2Frame::makeRawFrame(type, flags, streamid, payload);
  } else {
    // Empty frame
    return {};
  }
}

envoy::config::core::v3::Http2ProtocolOptions http2Settings() {
  envoy::config::core::v3::Http2ProtocolOptions options(
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions())
          .value());

  options.mutable_hpack_table_size()->set_value(MAX_HD_TABLE_BUF_SIZE);
  options.set_allow_metadata(true);
  return options;
}

class Http2Harness {
public:
  Http2Harness() : server_settings_(http2Settings()) {
    ON_CALL(mock_server_callbacks_, newStream(_, _))
        .WillByDefault(Invoke(
            [&](ResponseEncoder&, bool) -> RequestDecoder& { return orphan_request_decoder_; }));
  }

  void fuzzRequest(std::vector<Http2Frame>& frames, bool use_oghttp2) {
    TestScopedRuntime scoped_runtime;
    if (use_oghttp2) {
      scoped_runtime.mergeValues({{"envoy.reloadable_features.http2_use_oghttp2", "true"}});
    } else {
      scoped_runtime.mergeValues({{"envoy.reloadable_features.http2_use_oghttp2", "false"}});
    }
    Stats::Scope& scope = *stats_store_.rootScope();
    server_ = std::make_unique<Http2::ServerConnectionImpl>(
        mock_server_connection_, mock_server_callbacks_,
        Http2::CodecStats::atomicGet(http2_stats_, scope), random_, server_settings_,
        Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
        envoy::config::core::v3::HttpProtocolOptions::ALLOW, overload_manager_);

    Buffer::OwnedImpl payload;
    payload.add(Http2Frame::Preamble, 24);
    static Http2Frame emptySettingsFrame = Http2Frame::makeEmptySettingsFrame();
    payload.add(emptySettingsFrame.data(), emptySettingsFrame.size());
    for (const auto& frame : frames) {
      payload.add(frame.data(), frame.size());
    }
    Status status = server_->dispatch(payload);
  }

private:
  const envoy::config::core::v3::Http2ProtocolOptions server_settings_;
  Stats::IsolatedStoreImpl stats_store_;
  Http2::CodecStats::AtomicPtr http2_stats_;

  NiceMock<MockRequestDecoder> orphan_request_decoder_;
  NiceMock<Network::MockConnection> mock_server_connection_;
  NiceMock<MockServerConnectionCallbacks> mock_server_callbacks_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Server::MockOverloadManager> overload_manager_;

  ServerConnectionPtr server_;
};

static std::unique_ptr<Http2Harness> harness;
static void resetHarness() { harness = nullptr; }

// HTTP/2 fuzzer
//
// Uses a mix of structured HTTP/2 frames and junk frames for coverage

DEFINE_PROTO_FUZZER(const test::common::http::http2::Http2ConnectionFuzzCase& input) {
  if (harness == nullptr) {
    harness = std::make_unique<Http2Harness>();
    atexit(resetHarness);
  }

  // Convert input to Http2Frame wire format
  size_t n_frames = input.frames_size();
  if (n_frames == 0) {
    return;
  }
  std::vector<Http2Frame> frames;
  nghttp2_hd_deflater* deflater = nullptr;
  if (nghttp2_hd_deflate_new(&deflater, MAX_HD_TABLE_BUF_SIZE) != 0) {
    return;
  }
  for (auto& pbframe : input.frames()) {
    Http2Frame frame = pbToH2Frame(deflater, pbframe);
    frames.push_back(frame);
  }
  nghttp2_hd_deflate_del(deflater);

  harness->fuzzRequest(frames, true /* use oghttp2 rather than nghttp2 */);
  harness->fuzzRequest(frames, false);
}

} // namespace
} // namespace Http2
} // namespace Http
} // namespace Envoy
