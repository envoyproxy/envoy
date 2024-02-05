#include "test/extensions/filters/http/ext_proc/test_processor.h"

#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "test/test_common/network_utility.h"

#include "absl/strings/str_format.h"
#include "grpc++/server_builder.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

grpc::Status ProcessorWrapper::Process(
    grpc::ServerContext* ctx,
    grpc::ServerReaderWriter<envoy::service::ext_proc::v3::ProcessingResponse,
                             envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
  if (context_callback_) {
    (*context_callback_)(ctx);
  }
  callback_(stream);
  return grpc::Status::OK;
}

void TestProcessor::start(const Network::Address::IpVersion ip_version, ProcessingFunc cb,
                          absl::optional<ContextProcessingFunc> context_cb) {
  wrapper_ = std::make_unique<ProcessorWrapper>(cb, context_cb);
  grpc::ServerBuilder builder;
  builder.RegisterService(wrapper_.get());
  builder.AddListeningPort(
      absl::StrFormat("%s:0", Network::Test::getLoopbackAddressUrlString(ip_version)),
      grpc::InsecureServerCredentials(), &listening_port_);
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
