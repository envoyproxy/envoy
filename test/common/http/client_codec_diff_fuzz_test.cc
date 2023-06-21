#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/codec_client.h"
#include "source/common/http/codes.h"
#include "source/common/http/exception.h"
#include "source/common/http/utility.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/http/server_codec_diff_fuzz.pb.validate.h"
#include "test/common/upstream/utility.h"
#include "test/config/utility.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/header_validator.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace {
constexpr absl::string_view request_body = "The quick brown fox jumps over the lazy dog";
constexpr absl::string_view response_body = "0123456789aBcDeFgHiJkLmNoPqRsTuVwXyZ";
} // namespace

class CodecClientTestContext {
public:
  CodecClientTestContext(Http1ParserImpl codec_impl,
                         const test::common::http::ServerCodecDiffFuzzTestCase& input)
      : input_(input) {
    EXPECT_CALL(*connection_, connecting()).WillOnce(Return(true));
    EXPECT_CALL(*connection_, detectEarlyCloseWhenReadDisabled(false));
    EXPECT_CALL(*connection_, addConnectionCallbacks(_)).WillOnce(SaveArgAddress(&connection_cb_));
    EXPECT_CALL(*connection_, connect()).WillOnce(Invoke([this]() -> void {
      connection_cb_->onEvent(Network::ConnectionEvent::Connected);
    }));
    EXPECT_CALL(*connection_, close(_)).WillRepeatedly(Invoke([this]() -> void {
      connection_state_ = Network::Connection::State::Closed;
      connection_cb_->onEvent(Network::ConnectionEvent::LocalClose);
    }));
    EXPECT_CALL(*connection_, close(_, _)).WillRepeatedly(Invoke([this]() -> void {
      connection_state_ = Network::Connection::State::Closed;
      connection_cb_->onEvent(Network::ConnectionEvent::LocalClose);
    }));
    EXPECT_CALL(*connection_, state()).WillRepeatedly(ReturnPointee(&connection_state_));

    EXPECT_CALL(*connection_, addReadFilter(_))
        .WillOnce(
            Invoke([this](Network::ReadFilterSharedPtr filter) -> void { filter_ = filter; }));
    ON_CALL(*connection_, streamInfo()).WillByDefault(ReturnRef(stream_info_));
    ON_CALL(*connection_, write(_, _))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          written_wire_bytes_.append(data.toString());
          data.drain(data.length());
          out_end_stream_ = end_stream;
        }));

    EXPECT_CALL(decoder_, decodeHeaders_(_, _))
        .WillRepeatedly(Invoke([&](ResponseHeaderMapPtr& headers, bool end_stream) {
          response_headers_ = createHeaderMap<ResponseHeaderMapImpl>(*headers);
          response_end_stream_ = end_stream;
        }));

    EXPECT_CALL(decoder_, decodeData(_, _))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool end_stream) {
          response_body_ += data.toString();
          response_end_stream_ = end_stream;
        }));

    EXPECT_CALL(decoder_, decodeTrailers_(_))
        .WillRepeatedly(Invoke([&](ResponseTrailerMapPtr& trailers) {
          response_trailers_ = createHeaderMap<ResponseTrailerMapImpl>(*trailers);
          response_end_stream_ = true;
        }));

    EXPECT_CALL(stream_callbacks_, onResetStream(_, _))
        .WillRepeatedly(Invoke([this](StreamResetReason reason, absl::string_view details) {
          stream_reset_ = true;
          reset_reason_ = reason;
          reset_details_ = details;
        }));

    // EXPECT_CALL(dispatcher_, createTimer_(_));

    cluster_->max_response_headers_count_ = 200;
    envoy::config::core::v3::Http2ProtocolOptions http2_options;
    cluster_->http2_options_ = Envoy::Http2::Utility::initializeAndValidateOptions(
        envoy::config::core::v3::Http2ProtocolOptions());
    cluster_->http2_options_.set_allow_connect(true);
    cluster_->http2_options_.set_allow_metadata(true);
    cluster_->http3_options_ =
        ConfigHelper::http2ToHttp3ProtocolOptions(cluster_->http2_options_, 16 * 1024 * 1024);
    cluster_->http3_options_.set_allow_extended_connect(true);
    cluster_->http1_settings_.use_balsa_parser_ = codec_impl == Http1ParserImpl::BalsaParser;
    cluster_->http1_settings_.allow_absolute_url_ =
        input_.configuration().http1_options().allow_absolute_url();
    cluster_->http1_settings_.allow_chunked_length_ =
        input_.configuration().http1_options().allow_chunked_length();
    cluster_->http1_settings_.enable_trailers_ =
        input_.configuration().http1_options().enable_trailers();
    cluster_->http1_settings_.accept_http_10_ =
        input_.configuration().http1_options().accept_http_10();

    host_description_ =
        Upstream::makeTestHostDescription(cluster_, "tcp://127.0.0.1:80", time_system_);
    codec_ = std::make_unique<CodecClientProd>(CodecType::HTTP1,
                                               Network::ClientConnectionPtr(connection_),
                                               host_description_, dispatcher_, random_, nullptr);
  }

  void sendRequest() {
    Http::RequestEncoder& request_encoder = codec_->newStream(decoder_);
    request_encoder.getStream().addCallbacks(stream_callbacks_);

    RequestHeaderMapPtr request_headers = makeRequestHeaders();
    request_encoder_status_ = request_encoder.encodeHeaders(
        *request_headers, !(input_.send_request_body() || input_.has_request_trailers()));
    if (request_encoder_status_.ok()) {
      if (input_.send_request_body()) {
        Buffer::OwnedImpl buffer(request_body);
        request_encoder.encodeData(buffer, !input_.has_request_trailers());
      }
      if (input_.has_request_trailers()) {
        RequestTrailerMapPtr request_trailers = makeRequestTrailers();
        request_encoder.encodeTrailers(*request_trailers);
      }
    }
  }

  void sendResponse() {
    ASSERT(input_.has_response());
    std::string wire_bytes;
    // TODO(yanavlasov): get version from request
    wire_bytes = "HTTP/1.1 ";
    if (input_.response().has_status()) {
      absl::StrAppend(&wire_bytes, input_.response().status().value(), " ",
                      CodeUtility::toString(
                          static_cast<Code>(std::atoi(input_.response().status().value().c_str()))),
                      "\r\n");
    }
    bool chunked_encoding = false;
    for (const auto& header : input_.response().headers()) {
      if (absl::EqualsIgnoreCase(header.key(), "transfer-encoding") &&
          absl::StrContainsIgnoreCase(header.value(), "chunked")) {
        chunked_encoding = true;
      }
      std::string value(header.value());
      if (input_.send_response_body() && absl::EqualsIgnoreCase(header.key(), "content-length")) {
        value = std::to_string(response_body.size());
      }
      absl::StrAppend(&wire_bytes, header.key(), ": ", value, "\r\n");
    }

    absl::StrAppend(&wire_bytes, "\r\n");
    if (input_.send_response_body()) {
      if (chunked_encoding) {
        absl::StrAppend(&wire_bytes, absl::StrFormat("%X", response_body.size()), "\r\n",
                        response_body, "\r\n0\r\n");
        if (!input_.has_response_trailers()) {
          absl::StrAppend(&wire_bytes, "\r\n");
        }
      } else {
        absl::StrAppend(&wire_bytes, response_body);
      }
    }

    if (input_.has_response_trailers() && chunked_encoding) {
      if (!input_.send_response_body()) {
        absl::StrAppend(&wire_bytes, "0\r\n");
      }
      for (const auto& trailer : input_.response_trailers().trailers()) {
        absl::StrAppend(&wire_bytes, trailer.key(), ": ", trailer.value(), "\r\n");
      }
      absl::StrAppend(&wire_bytes, "\r\n");
    }

    std::cout << "~~~~~~~~~~~~~~~~~~~~~\n" << wire_bytes << std::endl;
    Buffer::OwnedImpl buffer(wire_bytes);
    filter_->onData(buffer, true);
  }

  RequestHeaderMapPtr makeRequestHeaders() {
    ASSERT(input_.has_request());
    RequestHeaderMapPtr headers = Http::RequestHeaderMapImpl::create();
    if (input_.request().has_scheme()) {
      headers->setScheme(input_.request().scheme().value());
    }
    if (input_.request().has_method()) {
      headers->setMethod(input_.request().method().value());
    }
    if (input_.request().has_path()) {
      headers->setPath(input_.request().path().value());
    }
    if (input_.request().has_authority()) {
      headers->setHost(input_.request().authority().value());
    }
    if (input_.request().has_protocol()) {
      headers->setProtocol(input_.request().protocol().value());
    }
    for (const auto& header : input_.request().headers()) {
      if (absl::EqualsIgnoreCase(header.key(), "transfer-encoding")) {
        // TODO(yanavlasov): remove this when H/1 client codec has ASSERT for this header addressed
        continue;
      }
      std::string value(header.value());
      if (input_.send_request_body() && absl::EqualsIgnoreCase(header.key(), "content-length")) {
        value = std::to_string(request_body.size());
      }
      Http::HeaderString key_string;
      key_string.setCopyUnvalidatedForTestOnly(header.key());
      Http::HeaderString value_string;
      value_string.setCopyUnvalidatedForTestOnly(value);
      headers->addViaMove(std::move(key_string), std::move(value_string));
    }
    return headers;
  }

  RequestTrailerMapPtr makeRequestTrailers() {
    ASSERT(input_.has_request_trailers());
    RequestTrailerMapPtr trailers = Http::RequestTrailerMapImpl::create();
    for (const auto& trailer : input_.request_trailers().trailers()) {
      Http::HeaderString key_string;
      key_string.setCopyUnvalidatedForTestOnly(trailer.key());
      Http::HeaderString value_string;
      value_string.setCopyUnvalidatedForTestOnly(trailer.value());
      trailers->addViaMove(std::move(key_string), std::move(value_string));
    }
    return trailers;
  }

  const test::common::http::ServerCodecDiffFuzzTestCase input_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Random::MockRandomGenerator> random_;
  Event::SimulatedTimeSystem time_system_;
  Network::ReadFilterSharedPtr filter_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_{new NiceMock<Upstream::MockClusterInfo>()};
  Network::ConnectionCallbacks* connection_cb_{nullptr};
  Upstream::HostDescriptionConstSharedPtr host_description_;
  Network::MockClientConnection* connection_{new NiceMock<Network::MockClientConnection>};
  Network::Connection::State connection_state_{Network::Connection::State::Open};
  std::unique_ptr<CodecClientProd> codec_;
  Http::MockStreamCallbacks stream_callbacks_;
  Http::MockResponseDecoder decoder_;
  Http::Status request_encoder_status_;
  std::string written_wire_bytes_;
  bool out_end_stream_{false};
  ResponseHeaderMapPtr response_headers_;
  std::string response_body_;
  ResponseTrailerMapPtr response_trailers_;
  std::string reset_details_;
  StreamResetReason reset_reason_;
  bool stream_reset_{false};
  bool response_end_stream_{false};
};

class CodecClientTest {
public:
  CodecClientTest(Http1ParserImpl codec1, Http1ParserImpl codec2,
                  const test::common::http::ServerCodecDiffFuzzTestCase& input)
      : codec_under_test_1_(codec1, input), codec_under_test_2_(codec2, input) {}

  void test() {
    codec_under_test_1_.sendRequest();
    codec_under_test_2_.sendRequest();
    FUZZ_ASSERT(codec_under_test_1_.request_encoder_status_ ==
                codec_under_test_2_.request_encoder_status_);
    FUZZ_ASSERT(codec_under_test_1_.written_wire_bytes_ == codec_under_test_2_.written_wire_bytes_);

    // std::cout << "--------------\n" << codec_under_test_1_.written_wire_bytes_ << std::endl;

    if (!codec_under_test_1_.written_wire_bytes_.empty() &&
        codec_under_test_1_.request_encoder_status_.ok() &&
        codec_under_test_1_.input_.has_response()) {
      codec_under_test_1_.sendResponse();
      codec_under_test_2_.sendResponse();

      FUZZ_ASSERT(codec_under_test_1_.stream_reset_ == codec_under_test_2_.stream_reset_);
      FUZZ_ASSERT(codec_under_test_1_.response_end_stream_ ==
                  codec_under_test_2_.response_end_stream_);
      FUZZ_ASSERT((codec_under_test_1_.response_headers_ != nullptr &&
                   codec_under_test_2_.response_headers_ != nullptr) ||
                  (codec_under_test_1_.response_headers_ == nullptr &&
                   codec_under_test_2_.response_headers_ == nullptr));

      if (codec_under_test_1_.response_headers_ != nullptr) {
        // When both codecs produced response headers they must be the same
        FUZZ_ASSERT(*codec_under_test_1_.response_headers_ ==
                    *codec_under_test_2_.response_headers_);
      }

      FUZZ_ASSERT(codec_under_test_1_.response_body_ == codec_under_test_2_.response_body_);

      FUZZ_ASSERT((codec_under_test_1_.response_trailers_ != nullptr &&
                   codec_under_test_2_.response_trailers_ != nullptr) ||
                  (codec_under_test_1_.response_trailers_ == nullptr &&
                   codec_under_test_2_.response_trailers_ == nullptr));

      if (codec_under_test_1_.response_trailers_ != nullptr) {
        // When both codecs produced response trailers they must be the same
        FUZZ_ASSERT(*codec_under_test_1_.response_trailers_ ==
                    *codec_under_test_2_.response_trailers_);
      }

      FUZZ_ASSERT(codec_under_test_1_.connection_state_ == codec_under_test_2_.connection_state_);
      FUZZ_ASSERT(codec_under_test_1_.stream_reset_ == codec_under_test_2_.stream_reset_);
      /*
      if (codec_under_test_1_.response_headers_) {
        std::cout << "++++++++++++++\n" << *codec_under_test_1_.response_headers_ << std::endl <<
      codec_under_test_1_.response_body_ << "\n\n";
      }
      if (codec_under_test_1_.response_trailers_) {
        std::cout << *codec_under_test_1_.response_trailers_ << std::endl;
      }
      */
    }
  }

  CodecClientTestContext codec_under_test_1_;
  CodecClientTestContext codec_under_test_2_;
};

DEFINE_PROTO_FUZZER(const test::common::http::ServerCodecDiffFuzzTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  } catch (const Envoy::ProtobufMessage::DeprecatedProtoFieldException& e) {
    ENVOY_LOG_MISC(debug, "DeprecatedProtoFieldException: {}", e.what());
    return;
  }

  if (!input.has_request()) {
    return;
  }

  CodecClientTest http1_test(Http1ParserImpl::HttpParser, Http1ParserImpl::BalsaParser, input);

  http1_test.test();
}

} // namespace Http
} // namespace Envoy
