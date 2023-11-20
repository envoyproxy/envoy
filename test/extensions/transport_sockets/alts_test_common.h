#pragma once

#include "envoy/network/address.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "gtest/gtest.h"

using ::grpc::gcp::HandshakerService;

class AltsTestCommon : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AltsProxyTest(Network::Address::IpVersion version) : version_(version){};

  void startFakeHandshakerService(const std::vector<HandshakerReq>& expected_requests,
                                  grpc::Status status_to_return,
                                  bool return_error_response = false) {
    server_address_ = absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":0");
    absl::Notification notification;
    server_thread_ = std::make_unique<std::thread>([this, &notification, expected_requests,
                                                    status_to_return, return_error_response]() {
      FakeHandshakerService fake_handshaker_service(expected_requests, status_to_return,
                                                    return_error_response);
      grpc::ServerBuilder server_builder;
      int listening_port = -1;
      server_builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials(),
                                      &listening_port);
      server_builder.RegisterService(&fake_handshaker_service);
      server_ = server_builder.BuildAndStart();
      EXPECT_THAT(server_, ::testing::NotNull());
      EXPECT_NE(listening_port, -1);
      server_address_ =
          absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":", listening_port);
      (&notification)->Notify();
      server_->Wait();
    });
    notification.WaitForNotification();
  }

  std::string serverAddress() { return server_address_; }

  std::shared_ptr<grpc::Channel> getChannel() {
    return grpc::CreateChannel(server_address_,
                               grpc::InsecureChannelCredentials()); // NOLINT
  }

private:
  std::string server_address_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<std::thread> server_thread_;
  Network::Address::IpVersion version_;
}
