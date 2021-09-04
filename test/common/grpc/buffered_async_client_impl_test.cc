#include "envoy/config/core/v3/grpc_service.pb.h"

#include "source/common/grpc/async_client_impl.h"
#include "source/common/grpc/buffered_async_client_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/proto/helloworld.pb.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Grpc {
namespace {

// class EnvoyBufferedAsyncClientImplTest : public testing::Test {
// public:
//   EnvoyBufferedAsyncClientImplTest()
//       : method_descriptor_(helloworld::Greeter::descriptor()->FindMethodByName("SayHello")) {

//     config.mutable_envoy_grpc()->set_cluster_name("test_cluster");

//     auto& initial_metadata_entry = *config.mutable_initial_metadata()->Add();
//     initial_metadata_entry.set_key("downstream-local-address");
//     initial_metadata_entry.set_value("%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%");

//     grpc_client_ = std::make_unique<AsyncClientImpl>(cm_, config, test_time_.timeSystem());
//     cm_.initializeThreadLocalClusters({"test_cluster"});
//     ON_CALL(cm_.thread_local_cluster_, httpAsyncClient()).WillByDefault(ReturnRef(http_client_));

//     buffered_grpc_client_ =
//         std::make_unique<BufferedAsyncClient<helloworld::HelloRequest, helloworld::HelloReply>>(
//             1000, *method_descriptor_, grpc_callbacks_,
//             Grpc::AsyncClient<helloworld::HelloRequest, helloworld::HelloReply>(grpc_client_));
//   }

//   envoy::config::core::v3::GrpcService config;
//   const Protobuf::MethodDescriptor* method_descriptor_;
//   NiceMock<Http::MockAsyncClient> http_client_;
//   NiceMock<Upstream::MockClusterManager> cm_;
//   AsyncClient<helloworld::HelloRequest, helloworld::HelloReply> grpc_client_;
//   std::unique_ptr<BufferedAsyncClient<helloworld::HelloRequest, helloworld::HelloReply>>
//       buffered_grpc_client_;
//   DangerousDeprecatedTestTime test_time_;
//   NiceMock<MockAsyncStreamCallbacks<helloworld::HelloReply>> grpc_callbacks_;
// };

// TEST_F(EnvoyBufferedAsyncClientImplTest, BasicFlow) {
//   helloworld::HelloRequest request_msg;
//   const auto id = buffered_grpc_client_->publishId(request_msg);
//   buffered_grpc_client_->bufferMessage(id, request_msg);

//   Http::AsyncClient::StreamCallbacks* http_callbacks;
//   Http::MockAsyncClientStream http_stream;
//   EXPECT_CALL(http_client_, start(_, _))
//       .WillOnce(
//           Invoke([&http_callbacks, &http_stream](Http::AsyncClient::StreamCallbacks& callbacks,
//                                                  const Http::AsyncClient::StreamOptions&) {
//             http_callbacks = &callbacks;
//             return &http_stream;
//           }));

//   EXPECT_CALL(grpc_callbacks_,
//               onCreateInitialMetadata(testing::Truly([](Http::RequestHeaderMap& headers) {
//                 return headers.Host()->value() == "test_cluster";
//               })));
//   EXPECT_CALL(http_stream, sendHeaders(_, _))
//       .WillOnce(Invoke([&http_callbacks](Http::HeaderMap&, bool) { http_callbacks->onReset();
//       }));
//   buffered_grpc_client_->sendBufferedMessages();
// }

} // namespace
} // namespace Grpc
} // namespace Envoy
