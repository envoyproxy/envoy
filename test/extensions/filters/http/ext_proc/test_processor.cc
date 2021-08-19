#include "test/extensions/filters/http/ext_proc/test_processor.h"

#include "envoy/service/ext_proc/v3alpha/external_processor.pb.h"

#include "grpc++/server_builder.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using Envoy::Network::Address::IpVersion;

grpc::Status ProcessorWrapper::Process(
    grpc::ServerContext*,
    grpc::ServerReaderWriter<envoy::service::ext_proc::v3alpha::ProcessingResponse,
                             envoy::service::ext_proc::v3alpha::ProcessingRequest>* stream) {
  callback_(stream);
  if (testing::Test::HasFatalFailure()) {
    // This is not strictly necessary, but it may help in troubleshooting to
    // ensure that we return a bad gRPC status if an "ASSERT" failed in the
    // processor.
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Fatal test error");
  }
  return grpc::Status::OK;
}

void TestProcessor::start(IpVersion ip_version, ProcessingFunc cb) {
  wrapper_ = std::make_unique<ProcessorWrapper>(cb);
  grpc::ServerBuilder builder;
  builder.RegisterService(wrapper_.get());
  const char* address = ip_version == IpVersion::v4 ? "127.0.0.1:0" : "[::1]:0";
  builder.AddListeningPort(address, grpc::InsecureServerCredentials(), &listening_port_);
  server_ = builder.BuildAndStart();
}

void TestProcessor::shutdown() {
  if (server_) {
    server_->Shutdown();
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
