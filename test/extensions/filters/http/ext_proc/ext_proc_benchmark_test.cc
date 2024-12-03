#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/common/perf_annotation.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/extensions/filters/http/ext_proc/test_processor.h"
#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

static const int DefaultTestIterations = 100;

/*
 * This file contains a set of tests that may be used to test the performance
 * of the ext_proc filter. It tests a set of common ext_proc operations by
 * sending synthetic HTTP requests to the server using the integration test
 * framework, and connecting ext_proc to a real gRPC server running in the
 * same process. This way we measure both the overhead of the ext_proc filter
 * and of the gRPC mechanism.
 *
 * To run, build the test with the bazel flag:
 *
 *    --define perf_annotation=enabled
 *
 * When built with this flag, the test will print out benchmark results
 * when it exits.
 *
 * By default, each test executes 100 times, which is likely not enough for
 * a consistent result. The environment variable
 * EXT_PROC_BENCHMARK_ITERATIONS may be used to override this.
 */
class BenchmarkTest : public HttpIntegrationTest,
                      public Grpc::BaseGrpcClientIntegrationParamTest,
                      public testing::Test {
protected:
  BenchmarkTest() : HttpIntegrationTest(Http::CodecType::HTTP2, getIpVersion()) {}

  Network::Address::IpVersion ipVersion() const override { return getIpVersion(); }

  static Network::Address::IpVersion getIpVersion() {
    return Network::Test::supportsIpVersion(Network::Address::IpVersion::v4)
               ? Network::Address::IpVersion::v4
               : Network::Address::IpVersion::v6;
  }

  Grpc::ClientType clientType() const override { return Grpc::ClientType::EnvoyGrpc; }

  static void TearDownTestSuite() { PERF_DUMP(); }

  void TearDown() override { test_processor_.shutdown(); }

  void initialize() override {
    // This enables a built-in automatic upstream server.
    autonomous_upstream_ = true;

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Create a cluster for our gRPC server pointing to the address that is running the gRPC
      // server.
      auto* processor_cluster = bootstrap.mutable_static_resources()->add_clusters();
      processor_cluster->set_name("ext_proc_server");
      processor_cluster->mutable_load_assignment()->set_cluster_name("ext_proc_server");
      auto* address = processor_cluster->mutable_load_assignment()
                          ->add_endpoints()
                          ->add_lb_endpoints()
                          ->mutable_endpoint()
                          ->mutable_address()
                          ->mutable_socket_address();
      address->set_address(Network::Test::getLoopbackAddressString(ipVersion()));
      address->set_port_value(test_processor_.port());

      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC.
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));
      ConfigHelper::setHttp2(*processor_cluster);

      // Make sure both flavors of gRPC client use the right address.
      const auto addr = Network::Test::getCanonicalLoopbackAddress(ipVersion());
      const auto addr_port = Network::Utility::getAddressWithPort(*addr, test_processor_.port());
      setGrpcService(*proto_config_.mutable_grpc_service(), "ext_proc_server", addr_port);

      // Merge the filter.
      envoy::config::listener::v3::Filter ext_proc_filter;
      ext_proc_filter.set_name("envoy.filters.http.ext_proc");
      ext_proc_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ext_proc_filter));
    });

    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
    HttpIntegrationTest::initialize();
  }

  int getTestIterations() const {
    const auto env_value = TestEnvironment::getOptionalEnvVar("EXT_PROC_BENCHMARK_ITERATIONS");
    int iterations;
    if (env_value && absl::SimpleAtoi(*env_value, &iterations)) {
      return iterations;
    }
    return DefaultTestIterations;
  }

  int testIterations() {
    static int iterations = getTestIterations();
    return iterations;
  }

  void measureHttpGets(absl::string_view test_name, int response_size = 100) {
    // The PERF_RECORD macro is weird and we need to use this variable for something
    // else or we will fail to compile.
    EXPECT_FALSE(test_name.empty());
    for (int iteration = 0; iteration < getTestIterations(); iteration++) {
      Http::TestRequestHeaderMapImpl headers;
      HttpTestUtility::addDefaultHeaders(headers);
      headers.addCopy(Http::LowerCaseString("response_size_bytes"), response_size);
      auto conn = makeClientConnection(lookupPort("http"));
      codec_client_ = makeHttpConnection(std::move(conn));

      PERF_OPERATION(op);
      auto client_response = codec_client_->makeHeaderOnlyRequest(headers);
      ASSERT_TRUE(client_response->waitForEndStream());
      EXPECT_TRUE(client_response->complete());
      EXPECT_THAT(client_response->headers(), Http::HttpStatusIs("200"));
      EXPECT_EQ(client_response->body().size(), response_size);
      PERF_RECORD(op, "benchmark", test_name);

      cleanupUpstreamAndDownstream();
    }
  }

  TestProcessor test_processor_;
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor proto_config_{};
};

// Skip sending to the external processor completely.
TEST_F(BenchmarkTest, NoProcessor) {
  proto_config_.mutable_processing_mode()->set_request_header_mode(ProcessingMode::SKIP);
  proto_config_.mutable_processing_mode()->set_response_header_mode(ProcessingMode::SKIP);
  test_processor_.start(ipVersion(),
                        [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>*) {
                          FAIL() << "Expected not to be called";
                        });
  initialize();
  measureHttpGets("no-processor");
}

// Close the stream as soon as the request headers come in.
TEST_F(BenchmarkTest, ImmediateClose) {
  test_processor_.start(ipVersion(),
                        [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>*) {});
  initialize();
  measureHttpGets("immediate-close");
}

// Add a request header, then close.
TEST_F(BenchmarkTest, AddRequestHeaderAndClose) {
  test_processor_.start(
      ipVersion(), [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest header_req;
        ASSERT_TRUE(stream->Read(&header_req));
        ASSERT_TRUE(header_req.has_request_headers());
        ProcessingResponse header_resp;
        auto* new_hdr = header_resp.mutable_request_headers()
                            ->mutable_response()
                            ->mutable_header_mutation()
                            ->add_set_headers();
        new_hdr->mutable_header()->set_key("x-envoy-benchmark");
        new_hdr->mutable_header()->set_raw_value("true");
        stream->Write(header_resp);
      });
  initialize();
  measureHttpGets("add-request-header-close");
}

// Add a response header, then close.
TEST_F(BenchmarkTest, AddResponseHeaderAndClose) {
  test_processor_.start(
      ipVersion(), [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest request_in;
        ASSERT_TRUE(stream->Read(&request_in));
        ASSERT_TRUE(request_in.has_request_headers());
        ProcessingResponse request_out;
        request_out.mutable_request_headers();
        stream->Write(request_out);

        ProcessingRequest response_in;
        ASSERT_TRUE(stream->Read(&response_in));
        ASSERT_TRUE(response_in.has_response_headers());
        ProcessingResponse response_out;
        auto* new_hdr = response_out.mutable_response_headers()
                            ->mutable_response()
                            ->mutable_header_mutation()
                            ->add_set_headers()
                            ->mutable_header();
        new_hdr->set_key("x-envoy-benchmark");
        new_hdr->set_raw_value("true");
        stream->Write(response_out);
      });
  initialize();
  measureHttpGets("add-response-header-close");
}

// Process the response body in buffered mode.
TEST_F(BenchmarkTest, ProcessBufferedResponseBody) {
  proto_config_.mutable_processing_mode()->set_response_body_mode(ProcessingMode::BUFFERED);
  test_processor_.start(
      ipVersion(), [](grpc::ServerReaderWriter<ProcessingResponse, ProcessingRequest>* stream) {
        ProcessingRequest request_in;
        ASSERT_TRUE(stream->Read(&request_in));
        ASSERT_TRUE(request_in.has_request_headers());
        ProcessingResponse request_out;
        request_out.mutable_request_headers();
        stream->Write(request_out);

        ProcessingRequest response_in;
        ASSERT_TRUE(stream->Read(&response_in));
        ASSERT_TRUE(response_in.has_response_headers());
        ProcessingResponse response_out;
        response_out.mutable_response_headers();
        stream->Write(response_out);

        ProcessingRequest body_in;
        ASSERT_TRUE(stream->Read(&body_in));
        ASSERT_TRUE(body_in.has_response_body());
        ProcessingResponse body_out;
        body_out.mutable_response_body();
        stream->Write(body_out);
      });
  initialize();
  measureHttpGets("buffered-response-body", 2000);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
