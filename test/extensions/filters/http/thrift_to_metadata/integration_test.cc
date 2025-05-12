#include "source/extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_converter.h"
#include "source/extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

using namespace Envoy::Extensions::NetworkFilters::ThriftProxy;

class ThriftToMetadataIntegrationTest : public Event::TestUsingSimulatedTime,
                                        public HttpProtocolIntegrationTest {
public:
  void initializeFilter() {
    config_helper_.prependFilter(filter_config_);
    initialize();
  }

  void runTest(const Http::RequestHeaderMap& request_headers, const std::string& request_body,
               const Http::ResponseHeaderMap& resp_headers, const std::string& response_body,
               const size_t chunk_size = 5, bool has_trailer = false) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response;
    if (request_body.empty()) {
      response = codec_client_->makeHeaderOnlyRequest(request_headers);
    } else {
      auto encoder_decoder = codec_client_->startRequest(request_headers);
      request_encoder_ = &encoder_decoder.first;
      response = std::move(encoder_decoder.second);
      size_t i = 0;
      for (; i < request_body.length() / chunk_size; i++) {
        Buffer::OwnedImpl buffer(request_body.substr(i * chunk_size, chunk_size));
        codec_client_->sendData(*request_encoder_, buffer, false);
      }
      // Send the last chunk flagged as end_stream.
      Buffer::OwnedImpl buffer(
          request_body.substr(i * chunk_size, request_body.length() % chunk_size));
      codec_client_->sendData(*request_encoder_, buffer, !has_trailer);

      if (has_trailer) {
        codec_client_->sendTrailers(*request_encoder_, rq_trailers_);
      }
    }
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    if (response_body.empty()) {
      upstream_request_->encodeHeaders(resp_headers, true);
    } else {
      upstream_request_->encodeHeaders(resp_headers, false);
      size_t i = 0;
      for (; i < response_body.length() / chunk_size; i++) {
        Buffer::OwnedImpl buffer(response_body.substr(i * chunk_size, chunk_size));
        upstream_request_->encodeData(buffer, false);
      }
      // Send the last chunk flagged as end_stream.
      Buffer::OwnedImpl buffer(
          response_body.substr(i * chunk_size, response_body.length() % chunk_size));
      upstream_request_->encodeData(buffer, !has_trailer);

      if (has_trailer) {
        upstream_request_->encodeTrailers(resp_trailers_);
      }
    }
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    // cleanup
    codec_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  void writeMessage(Buffer::OwnedImpl& buffer,
                    absl::optional<ReplyType> reply_type = absl::nullopt) {
    TransportType transport_type = TransportType::Unframed;
    ProtocolType protocol_type = ProtocolType::Binary;
    MessageType message_type = MessageType::Call;

    Buffer::OwnedImpl proto_buffer;
    ProtocolConverterSharedPtr protocol_converter = std::make_shared<ProtocolConverter>();
    ProtocolPtr protocol = createProtocol(protocol_type);
    protocol_converter->initProtocolConverter(*protocol, proto_buffer);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    metadata->setProtocol(protocol_type);
    metadata->setMethodName(method_name_);
    metadata->setMessageType(message_type);
    metadata->setSequenceId(1234);

    protocol_converter->messageBegin(metadata);
    protocol_converter->structBegin("");
    int16_t field_id = 0;
    FieldType field_type_string = FieldType::String;
    FieldType field_type_struct = FieldType::Struct;
    FieldType field_type_stop = FieldType::Stop;
    if (message_type == MessageType::Reply || message_type == MessageType::Exception) {
      if (reply_type.value() == ReplyType::Success) {
        field_id = 0;
        protocol_converter->fieldBegin("", field_type_string, field_id);
        protocol_converter->stringValue("value1");
        protocol_converter->fieldEnd();
      } else {
        // successful response struct in field id 0, error (IDL exception) in field id greater than
        // 0
        field_id = 2;
        protocol_converter->fieldBegin("", field_type_struct, field_id);
        protocol_converter->structBegin("");
        field_id = 1;
        protocol_converter->fieldBegin("", field_type_string, field_id);
        protocol_converter->stringValue("err");
        protocol_converter->fieldEnd();
        field_id = 0;
        protocol_converter->fieldBegin("", field_type_stop, field_id);
        protocol_converter->structEnd();
        protocol_converter->fieldEnd();
      }
    }
    field_id = 0;
    protocol_converter->fieldBegin("", field_type_stop, field_id);
    protocol_converter->structEnd();
    protocol_converter->messageEnd();

    TransportPtr transport = createTransport(transport_type);
    transport->encodeFrame(buffer, *metadata, proto_buffer);
  }

  TransportPtr createTransport(TransportType transport) {
    return NamedTransportConfigFactory::getFactory(transport).createTransport();
  }

  ProtocolPtr createProtocol(ProtocolType protocol) {
    return NamedProtocolConfigFactory::getFactory(protocol).createProtocol();
  }

  const std::string filter_config_ = R"EOF(
name: envoy.filters.http.thrift_to_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.thrift_to_metadata.v3.ThriftToMetadata
  request_rules:
  - field: METHOD_NAME
    on_present:
      metadata_namespace: envoy.lb
      key: method_name
    on_missing:
      metadata_namespace: envoy.lb
      key: method_name
      value: "unknown"
  response_rules:
  - field: MESSAGE_TYPE
    on_present:
      key: response_message_type
    on_missing:
      key: response_message_type
      value: "unknown"
  - field: REPLY_TYPE
    on_present:
      key: response_reply_type
    on_missing:
      key: response_reply_type
      value: "unknown"
)EOF";

  Http::TestRequestHeaderMapImpl rq_headers_{{":scheme", "http"},
                                             {":path", "/ping"},
                                             {":method", "POST"},
                                             {":authority", "host"},
                                             {"Content-Type", "application/x-thrift"}};
  Http::TestRequestTrailerMapImpl rq_trailers_{{"request1", "trailer1"}, {"request2", "trailer2"}};
  Http::TestResponseHeaderMapImpl resp_headers_{{":status", "200"},
                                                {"Content-Type", "application/x-thrift"}};
  Http::TestResponseTrailerMapImpl resp_trailers_{{"request1", "trailer1"},
                                                  {"request2", "trailer2"}};
  const std::string method_name_{"foo"};
  Buffer::OwnedImpl rq_buffer_;
  Buffer::OwnedImpl resp_buffer_;
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, ThriftToMetadataIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(ThriftToMetadataIntegrationTest, Basic) {
  initializeFilter();

  writeMessage(rq_buffer_);
  writeMessage(resp_buffer_, ReplyType::Success);
  runTest(rq_headers_, rq_buffer_.toString(), resp_headers_, resp_buffer_.toString());

  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.rq.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.invalid_thrift_body")->value());

  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.resp.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.invalid_thrift_body")->value());
}

TEST_P(ThriftToMetadataIntegrationTest, BasicOneChunk) {
  initializeFilter();

  writeMessage(rq_buffer_);
  writeMessage(resp_buffer_, ReplyType::Success);
  runTest(rq_headers_, rq_buffer_.toString(), resp_headers_, resp_buffer_.toString(), 1);

  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.rq.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.invalid_thrift_body")->value());

  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.resp.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.invalid_thrift_body")->value());
}

TEST_P(ThriftToMetadataIntegrationTest, Trailer) {
  initializeFilter();

  writeMessage(rq_buffer_);
  writeMessage(resp_buffer_, ReplyType::Success);
  runTest(rq_headers_, rq_buffer_.toString(), resp_headers_, resp_buffer_.toString(), 5, true);

  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.rq.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.invalid_thrift_body")->value());

  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.resp.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.invalid_thrift_body")->value());
}

TEST_P(ThriftToMetadataIntegrationTest, MismatchedContentType) {
  initializeFilter();

  const Http::TestRequestHeaderMapImpl rq_headers{{":scheme", "http"},
                                                  {":path", "/ping"},
                                                  {":method", "POST"},
                                                  {":authority", "host"},
                                                  {"Content-Type", "application/x-haha"}};
  Http::TestResponseHeaderMapImpl resp_headers{{":status", "200"},
                                               {"Content-Type", "application/x-haha"}};

  writeMessage(rq_buffer_);
  writeMessage(resp_buffer_, ReplyType::Success);
  runTest(rq_headers, rq_buffer_.toString(), resp_headers, resp_buffer_.toString());

  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.success")->value());
  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.rq.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.invalid_thrift_body")->value());

  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.success")->value());
  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.resp.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.invalid_thrift_body")->value());
}

TEST_P(ThriftToMetadataIntegrationTest, NoBody) {
  initializeFilter();

  const Http::TestRequestHeaderMapImpl rq_headers{{":scheme", "http"},
                                                  {":path", "/ping"},
                                                  {":method", "GET"},
                                                  {":authority", "host"},
                                                  {"Content-Type", "application/x-thrift"}};
  runTest(rq_headers, "", resp_headers_, "");

  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.mismatched_content_type")->value());
  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.rq.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.invalid_thrift_body")->value());

  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.mismatched_content_type")->value());
  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.resp.no_body")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.invalid_thrift_body")->value());
}

TEST_P(ThriftToMetadataIntegrationTest, InvalidThrift) {
  initializeFilter();

  runTest(rq_headers_, "it's not a thrift body", resp_headers_, "it's not a thrift body");

  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.rq.no_body")->value());
  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.rq.invalid_thrift_body")->value());

  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.success")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("thrift_to_metadata.resp.no_body")->value());
  EXPECT_EQ(1UL, test_server_->counter("thrift_to_metadata.resp.invalid_thrift_body")->value());
}

} // namespace
} // namespace Envoy
