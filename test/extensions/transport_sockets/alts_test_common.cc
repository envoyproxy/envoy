

void startFakeHandshakerService(const std::vector<HandshakerReq>& expected_requests,
                                grpc::Status status_to_return, bool return_error_response = false) {
  server_address_ = absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":0");
  absl::Notification notification;
  server_thread_ = std::make_unique<std::thread>(
      [this, &notification, expected_requests, status_to_return, return_error_response]() {
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
