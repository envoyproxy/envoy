#include <string>

#include "envoy/http/codec.h"
#include "envoy/service/network_ext_proc/v3/network_external_processor.pb.h"

#include "source/extensions/filters/network/ext_proc/config.h"

#include "test/config/utility.h"
#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

using envoy::service::network_ext_proc::v3::ProcessingRequest;
using envoy::service::network_ext_proc::v3::ProcessingResponse;

// Test-only filter that sets both typed and untyped connection metadata based on filter config
class MetadataSetterFilter : public Network::ReadFilter {
public:
  MetadataSetterFilter(const Protobuf::Struct& filter_config) : filter_config_(filter_config) {}

  Network::FilterStatus onNewConnection() override {
    // Set untyped metadata from config
    if (filter_config_.fields().contains("untyped_metadata")) {
      const auto& untyped_field = filter_config_.fields().at("untyped_metadata");
      if (untyped_field.has_struct_value()) {
        const auto& untyped_namespaces = untyped_field.struct_value();

        for (const auto& [namespace_name, metadata_struct] : untyped_namespaces.fields()) {
          if (metadata_struct.has_struct_value()) {
            callbacks_->connection().streamInfo().setDynamicMetadata(
                namespace_name, metadata_struct.struct_value());
          }
        }
      }
    }

    // We need to handle typed metadata differently
    if (filter_config_.fields().contains("typed_metadata")) {
      const auto& typed_field = filter_config_.fields().at("typed_metadata");
      if (typed_field.has_struct_value()) {
        const auto& typed_namespaces = typed_field.struct_value();

        for (const auto& [namespace_name, string_value] : typed_namespaces.fields()) {
          if (string_value.has_string_value()) {
            // Create a StringValue
            Protobuf::StringValue string_proto;
            string_proto.set_value(string_value.string_value());

            // Serialize to an Any
            Protobuf::Any typed_value;
            typed_value.PackFrom(string_proto);

            // Use the appropriate way to add typed metadata
            callbacks_->connection().streamInfo().setDynamicTypedMetadata(namespace_name,
                                                                          std::move(typed_value));
          }
        }
      }
    }

    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onData(Buffer::Instance&, bool) override {
    return Network::FilterStatus::Continue;
  }

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

private:
  Network::ReadFilterCallbacks* callbacks_{nullptr};
  const Protobuf::Struct& filter_config_;
};

class MetadataSetterFilterFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  absl::StatusOr<Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext&) override {
    const auto& struct_config = dynamic_cast<const Protobuf::Struct&>(proto_config);
    return [struct_config](Network::FilterManager& filter_manager) -> void {
      filter_manager.addReadFilter(std::make_shared<MetadataSetterFilter>(struct_config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "test.metadata_setter"; }
};

REGISTER_FACTORY(MetadataSetterFilterFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

class NetworkExtProcFilterIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  NetworkExtProcFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), absl::StrCat(Envoy::ConfigHelper::baseConfig(), R"EOF(
    filter_chains:
      filters:
        - name: test.metadata_setter
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
        - name: envoy.network_ext_proc.ext_proc_filter
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.ext_proc.v3.NetworkExternalProcessor
            grpc_service:
              envoy_grpc:
                cluster_name: "cluster_1"
            stat_prefix: "ext_proc_prefix"
        - name: envoy.filters.network.tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            stat_prefix: tcpproxy_stats
            cluster: cluster_0
)EOF")) {
    enableHalfClose(true);
    scoped_runtime_.mergeValues(
        {{"envoy.reloadable_features.connection_close_through_filter_manager", "true"}});
  }

  void createUpstreams() override {
    BaseIntegrationTest::createUpstreams();
    grpc_upstream_ = &addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initializeWithModifier(
      std::function<
          void(envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor&)>
          config_modifier = nullptr) {
    config_helper_.renameListener("network_ext_proc_filter");
    config_helper_.addConfigModifier(
        [&, config_modifier](::envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          // Common cluster setup
          auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
          cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          cluster->set_name("cluster_1");
          cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
          Envoy::ConfigHelper::setHttp2WithMaxConcurrentStreams(
              *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(1)), 2);

          // Apply config modifier if provided
          if (config_modifier) {
            // Find the network filter
            auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners(0);
            auto* filter_chain = listeners->mutable_filter_chains(0);
            auto* filters = filter_chain->mutable_filters();
            for (int i = 0; i < filters->size(); i++) {
              if ((*filters)[i].name() == "envoy.network_ext_proc.ext_proc_filter") {
                envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor config;
                (*filters)[i].mutable_typed_config()->UnpackTo(&config);

                // Apply the provided modifier function
                config_modifier(config);

                (*filters)[i].mutable_typed_config()->PackFrom(config);
                break;
              }
            }
          }
        });
    BaseIntegrationTest::initialize();
  }

  void initialize() override { initializeWithModifier(); }

  void initializeWithFailureModeAllow() {
    initializeWithModifier([](auto& config) { config.set_failure_mode_allow(true); });
  }

  void initializeWithSkipProcessingModes(bool skip_read) {
    initializeWithModifier([skip_read](auto& config) {
      auto* processing_mode = config.mutable_processing_mode();
      if (skip_read) {
        processing_mode->set_process_read(
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
      } else {
        processing_mode->set_process_write(
            envoy::extensions::filters::network::ext_proc::v3::ProcessingMode::SKIP);
      }
    });
  }

  void initializeWithMetadataOptions(const std::vector<std::string>& untyped_namespaces,
                                     const std::vector<std::string>& typed_namespaces) {
    initializeWithModifier([&untyped_namespaces, &typed_namespaces](auto& config) {
      auto* metadata_options = config.mutable_metadata_options();
      auto* forwarding_namespaces = metadata_options->mutable_forwarding_namespaces();

      for (const auto& ns : untyped_namespaces) {
        forwarding_namespaces->add_untyped(ns);
      }

      for (const auto& ns : typed_namespaces) {
        forwarding_namespaces->add_typed(ns);
      }
    });
  }

  // Helper method to set either untyped or typed connection metadata
  void setConnectionMetadata(const std::string& namespace_name,
                             const std::map<std::string, std::string>& untyped_values,
                             const absl::optional<std::string>& typed_value = absl::nullopt) {
    config_helper_.addConfigModifier([namespace_name, untyped_values, typed_value](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Find the metadata_setter filter
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* filter_chain = listener->mutable_filter_chains(0);
      auto* filters = filter_chain->mutable_filters();

      for (int i = 0; i < filters->size(); i++) {
        if ((*filters)[i].name() == "test.metadata_setter") {
          Protobuf::Struct existing_config;
          if ((*filters)[i].has_typed_config()) {
            (*filters)[i].typed_config().UnpackTo(&existing_config);
          }

          // Set untyped metadata
          if (!untyped_values.empty()) {
            Protobuf::Struct metadata_struct;
            auto* fields = metadata_struct.mutable_fields();

            for (const auto& [key, value] : untyped_values) {
              (*fields)[key].set_string_value(value);
            }

            Protobuf::Value namespace_value;
            *namespace_value.mutable_struct_value() = metadata_struct;

            if (!existing_config.fields().contains("untyped_metadata")) {
              Protobuf::Value untyped_value;
              existing_config.mutable_fields()->insert({"untyped_metadata", untyped_value});
            }

            auto* untyped_metadata =
                existing_config.mutable_fields()->at("untyped_metadata").mutable_struct_value();
            untyped_metadata->mutable_fields()->insert({namespace_name, namespace_value});
          }

          // Set typed metadata
          if (typed_value.has_value()) {
            if (!existing_config.fields().contains("typed_metadata")) {
              Protobuf::Value typed_value;
              existing_config.mutable_fields()->insert({"typed_metadata", typed_value});
            }

            auto* typed_metadata =
                existing_config.mutable_fields()->at("typed_metadata").mutable_struct_value();
            typed_metadata->mutable_fields()->insert({namespace_name, Protobuf::Value()});
            typed_metadata->mutable_fields()
                ->at(namespace_name)
                .set_string_value(typed_value.value());
          }

          (*filters)[i].mutable_typed_config()->PackFrom(existing_config);
          break;
        }
      }
    });
  }

  void waitForFirstGrpcMessage(ProcessingRequest& request) {
    ENVOY_LOG_MISC(debug, "waitForFirstGrpcMessage {}", grpc_upstream_->localAddress()->asString());
    ASSERT_TRUE(grpc_upstream_->waitForHttpConnection(*dispatcher_, processor_connection_));
    ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));
  }

  void sendReadGrpcMessage(std::string data, bool end_stream, bool first_message = false) {
    ProcessingResponse response;
    auto* read_data = response.mutable_read_data();
    read_data->set_data(data);
    read_data->set_end_of_stream(end_stream);
    ENVOY_LOG_MISC(debug, "Sending READ gRPC message: {}", response.DebugString());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    processor_stream_->sendGrpcMessage(response);
  }

  void sendReadGrpcMessageWithStream(Envoy::FakeStreamPtr& stream, std::string data,
                                     bool end_stream, bool first_message = false) {
    ProcessingResponse response;
    auto* read_data = response.mutable_read_data();
    read_data->set_data(data);
    read_data->set_end_of_stream(end_stream);
    ENVOY_LOG_MISC(debug, "Sending READ gRPC message via stream: {}", response.DebugString());
    if (first_message) {
      stream->startGrpcStream();
    }
    stream->sendGrpcMessage(response);
  }

  void sendWriteGrpcMessage(std::string data, bool end_stream, bool first_message = false) {
    ProcessingResponse response;
    auto* write_data = response.mutable_write_data();
    write_data->set_data(data);
    write_data->set_end_of_stream(end_stream);
    ENVOY_LOG_MISC(debug, "Sending WRITE gRPC message: {}", response.DebugString());
    if (first_message) {
      processor_stream_->startGrpcStream();
    }
    processor_stream_->sendGrpcMessage(response);
  }

  void sendWriteGrpcMessageWithStream(Envoy::FakeStreamPtr& stream, std::string data,
                                      bool end_stream, bool first_message = false) {
    ProcessingResponse response;
    auto* write_data = response.mutable_write_data();
    write_data->set_data(data);
    write_data->set_end_of_stream(end_stream);
    ENVOY_LOG_MISC(debug, "Sending WRITE gRPC message via stream: {}", response.DebugString());
    if (first_message) {
      stream->startGrpcStream();
    }
    stream->sendGrpcMessage(response);
  }

  void closeGrpcStream() {
    if (processor_stream_) {
      processor_stream_->encodeHeaders(
          Http::TestResponseHeaderMapImpl{{":status", "500"}, {"grpc-status", "8"}, {"foo", "bar"}},
          true);
    }
  }

  void verifyCounters(const std::map<std::string, uint64_t>& expected_counters) {
    for (const auto& [name, value] : expected_counters) {
      test_server_->waitForCounterGe("network_ext_proc.ext_proc_prefix." + name, value);
    }
  }

  void TearDown() override {
    if (processor_connection_) {
      ASSERT_TRUE(processor_connection_->close());
      ASSERT_TRUE(processor_connection_->waitForDisconnect());
    }
  }

  Envoy::FakeUpstream* grpc_upstream_;
  Envoy::FakeHttpConnectionPtr processor_connection_;
  Envoy::FakeStreamPtr processor_stream_;
  TestScopedRuntime scoped_runtime_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, NetworkExtProcFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test downstream connection closing
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyDownstreamClose) {
  initialize();
  Envoy::IntegrationTcpClientPtr tcp_client =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));

  Envoy::FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("client_data", true));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_read_data(), true);
  EXPECT_EQ(request.read_data().data(), "client_data");

  sendReadGrpcMessage("client_data_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));
  // The server should see the half-close
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  // Verify counters
  verifyCounters({{"streams_started", 1},
                  {"stream_msgs_sent", 1},
                  {"stream_msgs_received", 1},
                  {"read_data_sent", 1},
                  {"read_data_injected", 1}});

  tcp_client->close();
}

// Test default message timeout (200ms) handling for TCP proxy
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyDefaultMessageTimeout) {
  initialize();

  Envoy::IntegrationTcpClientPtr tcp_client =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));

  Envoy::FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send data from client
  ASSERT_TRUE(tcp_client->write("client_data_timeout_test", false));

  // Wait for the processing request from ext_proc filter
  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_read_data(), true);
  EXPECT_EQ(request.read_data().data(), "client_data_timeout_test");

  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(250));

  verifyCounters({{"streams_started", 1},
                  {"stream_msgs_sent", 1},
                  {"stream_msgs_received", 0}, // No response received due to timeout
                  {"read_data_sent", 1},
                  {"message_timeouts", 1}}); // Message timeout counter

  ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));

  tcp_client->close();
}

TEST_P(NetworkExtProcFilterIntegrationTest, MultipleClientConnections) {
  initialize();

  // max_concurrent_streams of the side stream cluster that is connected
  // to the gRPC server is 2.
  Envoy::IntegrationTcpClientPtr tcp_client_1 =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));
  Envoy::FakeRawConnectionPtr fake_upstream_connection_1;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_1));

  Envoy::IntegrationTcpClientPtr tcp_client_2 =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));
  Envoy::FakeRawConnectionPtr fake_upstream_connection_2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_2));

  Envoy::IntegrationTcpClientPtr tcp_client_3 =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));
  Envoy::FakeRawConnectionPtr fake_upstream_connection_3;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_3));

  ASSERT_TRUE(tcp_client_1->write("client_data_1", true));
  ProcessingRequest request_1;
  ASSERT_TRUE(grpc_upstream_->waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request_1));

  sendReadGrpcMessageWithStream(processor_stream_, "client_data_1_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection_1->waitForData(23));

  ASSERT_TRUE(tcp_client_2->write("client_data_2", true));
  Envoy::FakeStreamPtr processor_stream_2;

  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_2));
  ProcessingRequest request_2;
  ASSERT_TRUE(processor_stream_2->waitForGrpcMessage(*dispatcher_, request_2));
  sendReadGrpcMessageWithStream(processor_stream_2, "client_data_2_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection_2->waitForData(23));

  ASSERT_TRUE(tcp_client_3->write("client_data_3", true));
  // Max concurrent streams is 2, so a second connection will be created.
  Envoy::FakeHttpConnectionPtr processor_connection_2;
  ASSERT_TRUE(grpc_upstream_->waitForHttpConnection(*dispatcher_, processor_connection_2));
  Envoy::FakeStreamPtr processor_stream_3;
  ASSERT_TRUE(processor_connection_2->waitForNewStream(*dispatcher_, processor_stream_3));
  ProcessingRequest request_3;
  ASSERT_TRUE(processor_stream_3->waitForGrpcMessage(*dispatcher_, request_3));
  sendReadGrpcMessageWithStream(processor_stream_3, "client_data_3_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection_3->waitForData(23));

  // Verify counters - we should have processed 3 streams
  verifyCounters({{"streams_started", 3},
                  {"stream_msgs_sent", 3},
                  {"stream_msgs_received", 3},
                  {"read_data_sent", 3},
                  {"read_data_injected", 3}});

  tcp_client_1->close();
  tcp_client_2->close();
  ASSERT_TRUE(processor_connection_->close());
  ASSERT_TRUE(processor_connection_->waitForDisconnect());
  tcp_client_3->close();
  ASSERT_TRUE(processor_connection_2->close());
  ASSERT_TRUE(processor_connection_2->waitForDisconnect());
}

// Test that data is passed through both directions without data loss.
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyUpstreamHalfCloseBothWays) {
  initialize();

  // Connect to the server
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  // Write data from client to server
  ASSERT_TRUE(tcp_client->write("client_data", false));

  // Wait for the upstream connection and verify data
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_read_data(), true);
  EXPECT_EQ(request.read_data().data(), "client_data");
  EXPECT_EQ(request.read_data().end_of_stream(), false);

  sendReadGrpcMessage("client_data_inspected", true, true);

  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  // Write data from server to client
  ASSERT_TRUE(fake_upstream_connection->write("server_response", true));

  ProcessingRequest write_request;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, write_request));

  if (!write_request.write_data().end_of_stream()) {
    size_t total_upstream_data = 0;
    // We got partial data without end_of_stream
    std::string partial_data = write_request.write_data().data();
    std::string partial_response = partial_data + "_inspected";
    sendWriteGrpcMessage(partial_response, false);

    // Wait for client to receive the partial data
    total_upstream_data += partial_response.length();
    ASSERT_TRUE(tcp_client->waitForData(total_upstream_data));

    // Wait for the final message with end_of_stream
    ProcessingRequest final_request;
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, final_request));
    EXPECT_EQ(final_request.has_write_data(), true);
    EXPECT_EQ(final_request.write_data().end_of_stream(), true);

    // Respond to the final message
    std::string final_data = final_request.write_data().data();
    std::string final_response = final_data.empty() ? "" : final_data + "_inspected";
    sendReadGrpcMessage(final_response, true);

    // Wait for the final data if non-empty
    if (!final_response.empty()) {
      total_upstream_data += final_response.length();
      ASSERT_TRUE(tcp_client->waitForData(total_upstream_data));
    }
  } else {
    // We got the complete data with end_of_stream in one message
    EXPECT_EQ(write_request.write_data().data(), "server_response");
    EXPECT_EQ(write_request.write_data().end_of_stream(), true);
    sendWriteGrpcMessage("server_data_inspected", true);
    tcp_client->waitForData("server_data_inspected");
  }

  // Close everything
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(processor_stream_->waitForEndStream(*dispatcher_));

  // Verify bidirectional data counters
  verifyCounters({{"streams_started", 1},
                  {"stream_msgs_sent", 2},     // One for read, one for write
                  {"stream_msgs_received", 2}, // One for read, one for write
                  {"read_data_sent", 1},
                  {"read_data_injected", 1},
                  {"write_data_sent", 1},
                  {"write_data_injected", 1}});
  tcp_client->close();
}

// Test that data is passed through both directions without data loss.
TEST_P(NetworkExtProcFilterIntegrationTest, TcpProxyDownstreamHalfCloseBothWays) {
  initialize();

  Envoy::IntegrationTcpClientPtr tcp_client =
      makeTcpConnection(lookupPort("network_ext_proc_filter"));

  Envoy::FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(fake_upstream_connection->write("server_data", true));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_write_data(), true);
  EXPECT_EQ(request.write_data().data(), "server_data");
  EXPECT_EQ(request.write_data().end_of_stream(), true);

  sendWriteGrpcMessage("server_data_inspected", true, true);

  tcp_client->waitForData("server_data_inspected");

  // Use true here, and listener connection will get remote close.
  // and the disableClose(true) will take effect to delay the deletion of the filter chain.
  ASSERT_TRUE(tcp_client->write("client_data", true));

  // Track total data received by upstream
  size_t total_upstream_data = 0;

  // Process read data - handle potential TCP fragmentation
  ProcessingRequest write_request;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, write_request));
  EXPECT_EQ(write_request.has_read_data(), true);

  // Handle potential TCP fragmentation for client data
  if (!write_request.read_data().end_of_stream()) {
    // We got partial data without end_of_stream
    std::string partial_data = write_request.read_data().data();
    std::string partial_response = partial_data + "_inspected";
    sendReadGrpcMessage(partial_response, false);

    // Wait for upstream to receive the partial data
    total_upstream_data += partial_response.length();
    ASSERT_TRUE(fake_upstream_connection->waitForData(total_upstream_data));

    // Wait for the final message with end_of_stream
    ProcessingRequest final_request;
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, final_request));
    EXPECT_EQ(final_request.has_read_data(), true);
    EXPECT_EQ(final_request.read_data().end_of_stream(), true);

    // Respond to the final message
    std::string final_data = final_request.read_data().data();
    std::string final_response = final_data.empty() ? "" : final_data + "_inspected";
    sendReadGrpcMessage(final_response, true);

    // Wait for the final data if non-empty
    if (!final_response.empty()) {
      total_upstream_data += final_response.length();
      ASSERT_TRUE(fake_upstream_connection->waitForData(total_upstream_data));
    }
  } else {
    // We got the complete data with end_of_stream in one message
    EXPECT_EQ(write_request.read_data().data(), "client_data");
    EXPECT_EQ(write_request.read_data().end_of_stream(), true);

    sendReadGrpcMessage("client_data_inspected", true);
    ASSERT_TRUE(fake_upstream_connection->waitForData(21)); // "client_data_inspected"
  }

  // Wait for the upstream to see the half-close
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Verify bidirectional data counters
  verifyCounters({{"streams_started", 1},
                  {"stream_msgs_sent", 2},     // At least 2 (could be more with fragmentation)
                  {"stream_msgs_received", 2}, // At least 2 (could be more with fragmentation)
                  {"read_data_sent", 1},       // At least 1
                  {"read_data_injected", 1},   // At least 1
                  {"write_data_sent", 1},
                  {"write_data_injected", 1}});

  tcp_client->close();
}

// Test behavior when the gRPC stream fails
TEST_P(NetworkExtProcFilterIntegrationTest, GrpcStreamFailure) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Close gRPC stream with an error status instead of Ok
  // This should trigger onGrpcError instead of onGrpcClose
  closeGrpcStream();
  test_server_->waitForCounterGe("network_ext_proc.ext_proc_prefix.streams_grpc_error", 1);
  ASSERT_FALSE(tcp_client->write("", true));
  tcp_client->waitForDisconnect();

  // Verify failure counters
  verifyCounters({{"streams_started", 1},
                  {"stream_msgs_sent", 1},
                  {"streams_grpc_error", 1},
                  {"connections_closed", 1},
                  {"read_data_sent", 1}});
}

// Test behavior when failure_mode_allow is set to true and gRPC stream fails
TEST_P(NetworkExtProcFilterIntegrationTest, GrpcStreamFailureWithFailureModeAllow) {
  initializeWithFailureModeAllow();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ASSERT_TRUE(tcp_client->write("client_data", false));
  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Close gRPC stream without sending a response - with failure_mode_allow=true
  // the connection should not be closed
  closeGrpcStream();

  // Wait for the failure_mode_allowed counter to increment
  test_server_->waitForCounterGe("network_ext_proc.ext_proc_prefix.failure_mode_allowed", 1);

  // Should be able to continue using the connection after stream failure
  ASSERT_TRUE(tcp_client->write("more_data", true));
  ASSERT_TRUE(fake_upstream_connection->waitForData(9));

  tcp_client->close();
}

// Test with read processing modes set to SKIP
TEST_P(NetworkExtProcFilterIntegrationTest, ProcessingModesReadSkip) {
  initializeWithSkipProcessingModes(true);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  // Write data from client to server - with process_read=SKIP, this should pass through
  // directly without invoking the external processor
  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Data should be passed through without modification
  ASSERT_TRUE(fake_upstream_connection->waitForData(11));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test with write processing modes set to SKIP
TEST_P(NetworkExtProcFilterIntegrationTest, ProcessingModesWriteSkip) {
  initializeWithSkipProcessingModes(false);
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Data should be passed through without modification
  ASSERT_TRUE(fake_upstream_connection->write("server_response", false));
  tcp_client->waitForData("server_response");

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test handling of multiple data chunks in a single direction
TEST_P(NetworkExtProcFilterIntegrationTest, MultipleDataChunks) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  ASSERT_TRUE(tcp_client->write("chunk1", false));

  // Wait for the upstream connection
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // First chunk should be sent to ext_proc
  ProcessingRequest request1;
  waitForFirstGrpcMessage(request1);
  EXPECT_EQ(request1.has_read_data(), true);
  EXPECT_EQ(request1.read_data().data(), "chunk1");
  EXPECT_EQ(request1.read_data().end_of_stream(), false);

  sendReadGrpcMessage("chunk1_processed", false, true);
  size_t total_upstream_data = 16; // Already received "chunk1_processed"
  ASSERT_TRUE(fake_upstream_connection->waitForData(total_upstream_data)); // "chunk1_processed"

  ASSERT_TRUE(tcp_client->write("chunk2", true));

  ProcessingRequest request2;
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request2));
  EXPECT_EQ(request2.has_read_data(), true);

  // Handle potential TCP fragmentation
  if (!request2.read_data().end_of_stream()) {
    // We got partial data without end_of_stream
    std::string partial_response = request2.read_data().data() + "_processed";
    sendReadGrpcMessage(partial_response, false);

    // Wait for upstream to receive the partial data
    total_upstream_data += partial_response.length();
    ASSERT_TRUE(fake_upstream_connection->waitForData(total_upstream_data));

    // Wait for the final message with end_of_stream
    ProcessingRequest request3;
    ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request3));
    EXPECT_EQ(request3.has_read_data(), true);
    EXPECT_EQ(request3.read_data().end_of_stream(), true);

    // Respond to the final message
    std::string final_response = request3.read_data().data() + "_processed";
    sendReadGrpcMessage(final_response, true);

    // Wait for the final data if non-empty
    if (!request3.read_data().data().empty()) {
      total_upstream_data += final_response.length();
      ASSERT_TRUE(fake_upstream_connection->waitForData(total_upstream_data));
    }
  } else {
    // We got the complete chunk2 with end_of_stream in one message
    EXPECT_EQ(request2.read_data().data(), "chunk2");
    EXPECT_EQ(request2.read_data().end_of_stream(), true);

    sendReadGrpcMessage("chunk2_processed", true);

    total_upstream_data += 16; // "chunk2_processed"
    ASSERT_TRUE(fake_upstream_connection->waitForData(total_upstream_data));
  }

  // Wait for half-close to ensure end_of_stream was properly propagated
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test empty data handling
TEST_P(NetworkExtProcFilterIntegrationTest, EmptyDataHandling) {
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Send empty data with end_stream=true to indicate half-close
  ASSERT_TRUE(tcp_client->write("", true));

  // Empty data should still trigger ext_proc
  ProcessingRequest request;
  waitForFirstGrpcMessage(request);
  EXPECT_EQ(request.has_read_data(), true);
  EXPECT_EQ(request.read_data().data(), "");
  EXPECT_EQ(request.read_data().end_of_stream(), true);

  // Send back an empty response
  sendReadGrpcMessage("", true, true);

  // Server should see half-close
  ASSERT_TRUE(fake_upstream_connection->waitForHalfClose());

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test dynamic metadata forwarding with untyped metadata
TEST_P(NetworkExtProcFilterIntegrationTest, UntypedMetadataForwarding) {
  setConnectionMetadata("test-namespace", {{"key1", "value1"}, {"key2", "value2"}});
  initializeWithMetadataOptions({"test-namespace"}, {});

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Check that the request includes the expected metadata
  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Verify metadata is present
  EXPECT_TRUE(request.has_metadata());
  EXPECT_TRUE(request.metadata().filter_metadata().contains("test-namespace"));

  // Verify metadata values
  const auto& metadata = request.metadata().filter_metadata().at("test-namespace");
  EXPECT_TRUE(metadata.fields().contains("key1"));
  EXPECT_TRUE(metadata.fields().contains("key2"));
  EXPECT_EQ(metadata.fields().at("key1").string_value(), "value1");
  EXPECT_EQ(metadata.fields().at("key2").string_value(), "value2");

  sendReadGrpcMessage("client_data_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test dynamic metadata forwarding with multiple untyped namespaces
TEST_P(NetworkExtProcFilterIntegrationTest, MultipleUntypedNamespaces) {
  setConnectionMetadata("namespace1", {{"key1", "value1"}});
  setConnectionMetadata("namespace2", {{"key2", "value2"}});
  // Should not be forwarded
  setConnectionMetadata("namespace3", {{"key3", "value3"}});

  // Initialize with metadata options
  initializeWithMetadataOptions({"namespace1", "namespace2"}, {});

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Verify metadata is present
  EXPECT_TRUE(request.has_metadata());
  EXPECT_TRUE(request.metadata().filter_metadata().contains("namespace1"));
  EXPECT_TRUE(request.metadata().filter_metadata().contains("namespace2"));
  EXPECT_FALSE(request.metadata().filter_metadata().contains("namespace3"));

  // Verify metadata values
  const auto& metadata1 = request.metadata().filter_metadata().at("namespace1");
  EXPECT_TRUE(metadata1.fields().contains("key1"));
  EXPECT_EQ(metadata1.fields().at("key1").string_value(), "value1");

  const auto& metadata2 = request.metadata().filter_metadata().at("namespace2");
  EXPECT_TRUE(metadata2.fields().contains("key2"));
  EXPECT_EQ(metadata2.fields().at("key2").string_value(), "value2");

  sendReadGrpcMessage("client_data_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test with no metadata forwarding configuration
TEST_P(NetworkExtProcFilterIntegrationTest, NoMetadataForwarding) {
  // Add metadata before initializing
  setConnectionMetadata("test-namespace", {{"key1", "value1"}});
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Verify no metadata is present
  EXPECT_FALSE(request.has_metadata());

  sendReadGrpcMessage("client_data_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test with metadata forwarding configuration but no matching metadata
TEST_P(NetworkExtProcFilterIntegrationTest, NoMatchingMetadata) {
  // Add metadata before initializing
  setConnectionMetadata("other-namespace", {{"key1", "value1"}});

  // Initialize with metadata options
  initializeWithMetadataOptions({"forwarded-namespace"}, {});

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Verify no metadata is present since there's no matching namespace
  EXPECT_FALSE(request.has_metadata());

  sendReadGrpcMessage("client_data_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test typed metadata forwarding with StringValue (simplified approach)
TEST_P(NetworkExtProcFilterIntegrationTest, TypedMetadataForwarding) {
  // Add typed metadata for testing
  setConnectionMetadata("typed-namespace", {}, "hello-world");

  // Initialize with metadata options
  initializeWithMetadataOptions({}, {"typed-namespace"});

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Check the request
  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Verify typed metadata is present
  EXPECT_TRUE(request.has_metadata());
  EXPECT_TRUE(request.metadata().typed_filter_metadata().contains("typed-namespace"));

  // Verify typed metadata values
  const auto& typed_metadata = request.metadata().typed_filter_metadata().at("typed-namespace");
  EXPECT_EQ(typed_metadata.type_url(), "type.googleapis.com/google.protobuf.StringValue");

  // Deserialize the StringValue to verify the content
  Protobuf::StringValue string_value;
  EXPECT_TRUE(string_value.ParseFromString(typed_metadata.value()));
  EXPECT_EQ(string_value.value(), "hello-world");

  // Continue normal flow
  sendReadGrpcMessage("client_data_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test both typed and untyped metadata forwarding
TEST_P(NetworkExtProcFilterIntegrationTest, BothTypedAndUntypedMetadataForwarding) {
  // Add both untyped and typed metadata in a single call
  setConnectionMetadata("untyped-ns", {{"key1", "value1"}});
  setConnectionMetadata("typed-ns", {}, "typed-test-value");

  // Initialize with both typed and untyped metadata namespaces
  initializeWithMetadataOptions({"untyped-ns"}, {"typed-ns"});

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));

  ASSERT_TRUE(tcp_client->write("client_data", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // Check the request
  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Verify metadata is present
  EXPECT_TRUE(request.has_metadata());

  // Verify untyped metadata
  EXPECT_TRUE(request.metadata().filter_metadata().contains("untyped-ns"));
  const auto& untyped_metadata = request.metadata().filter_metadata().at("untyped-ns");
  EXPECT_TRUE(untyped_metadata.fields().contains("key1"));
  EXPECT_EQ(untyped_metadata.fields().at("key1").string_value(), "value1");

  // Verify typed metadata
  EXPECT_TRUE(request.metadata().typed_filter_metadata().contains("typed-ns"));
  const auto& typed_metadata = request.metadata().typed_filter_metadata().at("typed-ns");
  EXPECT_EQ(typed_metadata.type_url(), "type.googleapis.com/google.protobuf.StringValue");

  // Deserialize the StringValue
  Protobuf::StringValue string_value;
  EXPECT_TRUE(string_value.ParseFromString(typed_metadata.value()));
  EXPECT_EQ(string_value.value(), "typed-test-value");

  // Continue normal flow
  sendReadGrpcMessage("client_data_inspected", true, true);
  ASSERT_TRUE(fake_upstream_connection->waitForData(21));

  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
}

// Test connection status CLOSE handling in responses
TEST_P(NetworkExtProcFilterIntegrationTest, ConnectionStatusCloseHandling) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  ASSERT_TRUE(tcp_client->write("client_data", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  waitForFirstGrpcMessage(request);

  // Create a response with CLOSE status
  ProcessingResponse response;
  response.set_connection_status(ProcessingResponse::CLOSE);
  auto* read_data = response.mutable_read_data();
  read_data->set_data("modified_data");
  read_data->set_end_of_stream(false);

  processor_stream_->startGrpcStream();
  processor_stream_->sendGrpcMessage(response);

  // Verify counters
  verifyCounters({{"connections_closed", 1}});

  // Connection should be closed
  ASSERT_FALSE(tcp_client->write("", true));
  tcp_client->waitForDisconnect();
}

// Test connection status CLOSE_RST handling in responses
TEST_P(NetworkExtProcFilterIntegrationTest, ConnectionStatusRSTHandling) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("network_ext_proc_filter"));
  ASSERT_TRUE(tcp_client->write("client_data", false));

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  ProcessingRequest request;
  ASSERT_TRUE(grpc_upstream_->waitForHttpConnection(*dispatcher_, processor_connection_));
  ASSERT_TRUE(processor_connection_->waitForNewStream(*dispatcher_, processor_stream_));
  ASSERT_TRUE(processor_stream_->waitForGrpcMessage(*dispatcher_, request));

  // Create a response with CLOSE_RST status
  ProcessingResponse response;
  response.set_connection_status(ProcessingResponse::CLOSE_RST);
  auto* read_data = response.mutable_read_data();
  read_data->set_data("modified_data");
  read_data->set_end_of_stream(false);

  processor_stream_->startGrpcStream();
  processor_stream_->sendGrpcMessage(response);

  // Verify counters - should now have 1 close in total and 1 reset
  verifyCounters({{"connections_closed", 1}, {"connections_reset", 1}});

  // Connection should be closed
  ASSERT_FALSE(tcp_client->write("", true));
  tcp_client->waitForDisconnect();
}

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
