#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener.pb.validate.h"

#include "test/extensions/config_subscription/filesystem/filesystem_subscription_test_harness.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/logging.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::Throw;

namespace Envoy {
namespace Config {
namespace {

class FilesystemSubscriptionImplTest : public testing::Test,
                                       public FilesystemSubscriptionTestHarness {};

// Validate that the client can recover from bad JSON responses.
TEST_F(FilesystemSubscriptionImplTest, BadJsonRecovery) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, ""));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _));
  updateFile(";!@#badjso n");
  EXPECT_TRUE(statsAre(2, 0, 0, 1, 0, 0, 0, ""));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 1, 0, 1, 0, TEST_TIME_MILLIS, 7148434200721666028, "0"));
}

// Validate that a file that is initially available results in a successful update.
TEST_F(FilesystemSubscriptionImplTest, InitialFile) {
  updateFile("{\"versionInfo\": \"0\", \"resources\": []}", false);
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 1, 0, 0, 0, TEST_TIME_MILLIS, 7148434200721666028, "0"));
}

// Validate that if we fail to set a watch, we get a sensible warning.
TEST(MiscFilesystemSubscriptionImplTest, BadWatch) {
  Event::MockDispatcher dispatcher;
  Stats::MockIsolatedStatsStore stats_store;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  Api::ApiPtr api = Api::createApiForTest(stats_store);
  SubscriptionStats stats{Utility::generateStats(*stats_store.rootScope())};
  auto* watcher = new Filesystem::MockWatcher();
  EXPECT_CALL(dispatcher, createFilesystemWatcher_()).WillOnce(Return(watcher));
  EXPECT_CALL(*watcher, addWatch(_, _, _)).WillOnce(Throw(EnvoyException("bad path")));
  NiceMock<Config::MockSubscriptionCallbacks> callbacks;
  OpaqueResourceDecoderSharedPtr resource_decoder(
      std::make_shared<NiceMock<Config::MockOpaqueResourceDecoder>>());
  EXPECT_THROW_WITH_MESSAGE(
      FilesystemSubscriptionImpl(dispatcher, makePathConfigSource("##!@/dev/null"), callbacks,
                                 resource_decoder, stats, validation_visitor, *api),
      EnvoyException, "bad path");
}

// Validate that the update_time statistic isn't changed when the configuration update gets
// rejected.
TEST_F(FilesystemSubscriptionImplTest, UpdateTimeNotChangedOnUpdateReject) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, ""));
  EXPECT_CALL(callbacks_,
              onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, _));
  updateFile(";!@#badjso n");
  EXPECT_TRUE(statsAre(2, 0, 0, 1, 0, 0, 0, ""));
}

// Validate that the update_time statistic is changed after a trivial configuration update
// (update that resulted in no change).
TEST_F(FilesystemSubscriptionImplTest, UpdateTimeChangedOnUpdateSuccess) {
  startSubscription({"cluster0", "cluster1"});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, 0, 0, ""));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 0, TEST_TIME_MILLIS, 7148434200721666028, "0"));
  // Advance the simulated time.
  simTime().setSystemTime(SystemTime(std::chrono::milliseconds(TEST_TIME_MILLIS + 1)));
  deliverConfigUpdate({"cluster0", "cluster1"}, "0", true);
  EXPECT_TRUE(statsAre(3, 2, 0, 0, 0, TEST_TIME_MILLIS + 1, 7148434200721666028, "0"));
}

// TODO(htuch): Add generic test harness support for collection subscriptions so that we can test
// gRPC/HTTP transports similar to below.
class FilesystemCollectionSubscriptionImplTest : public testing::Test,
                                                 Event::TestUsingSimulatedTime {
public:
  FilesystemCollectionSubscriptionImplTest()
      : path_(makePathConfigSource(TestEnvironment::temporaryPath("lds.yaml"))),
        stats_(Utility::generateStats(*stats_store_.rootScope())),
        api_(Api::createApiForTest(stats_store_, simTime())), dispatcher_(setupDispatcher()),
        resource_decoder_(
            std::make_shared<
                TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::listener::v3::Listener>>(
                "name")),
        subscription_(*dispatcher_, path_, callbacks_, resource_decoder_, stats_,
                      ProtobufMessage::getStrictValidationVisitor(), *api_) {}
  ~FilesystemCollectionSubscriptionImplTest() override {
    TestEnvironment::removePath(path_.path());
  }

  Event::DispatcherPtr setupDispatcher() {
    auto dispatcher = std::make_unique<Event::MockDispatcher>();
    EXPECT_CALL(*dispatcher, createFilesystemWatcher_()).WillOnce(InvokeWithoutArgs([this] {
      Filesystem::MockWatcher* mock_watcher = new Filesystem::MockWatcher();
      EXPECT_CALL(*mock_watcher, addWatch(path_.path(), Filesystem::Watcher::Events::MovedTo, _))
          .WillOnce(
              Invoke([this](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
                on_changed_cb_ = cb;
                return absl::OkStatus();
              }));
      return mock_watcher;
    }));
    return dispatcher;
  }

  void updateFile(const std::string& yaml) {
    // Write YAML contents to file, rename to path_ and invoke on change callback
    const std::string temp_path = TestEnvironment::writeStringToFileForTest("lds.yaml.tmp", yaml);
    TestEnvironment::renameFile(temp_path, path_.path());
    EXPECT_TRUE(on_changed_cb_(Filesystem::Watcher::Events::MovedTo).ok());
  }

  AssertionResult statsAre(uint32_t attempt, uint32_t success, uint32_t rejected, uint32_t failure,
                           uint64_t version, absl::string_view version_text) {
    if (attempt != stats_.update_attempt_.value()) {
      return testing::AssertionFailure() << "update_attempt: expected " << attempt << ", got "
                                         << stats_.update_attempt_.value();
    }
    if (success != stats_.update_success_.value()) {
      return testing::AssertionFailure() << "update_success: expected " << success << ", got "
                                         << stats_.update_success_.value();
    }
    if (rejected != stats_.update_rejected_.value()) {
      return testing::AssertionFailure() << "update_rejected: expected " << rejected << ", got "
                                         << stats_.update_rejected_.value();
    }
    // The first attempt always fail.
    if (1 + failure != stats_.update_failure_.value()) {
      return testing::AssertionFailure() << "update_failure: expected " << 1 + failure << ", got "
                                         << stats_.update_failure_.value();
    }
    if (version != stats_.version_.value()) {
      return testing::AssertionFailure()
             << "version: expected " << version << ", got " << stats_.version_.value();
    }
    if (version_text != stats_.version_text_.value()) {
      return testing::AssertionFailure()
             << "version_text: expected " << version << ", got " << stats_.version_text_.value();
    }
    return testing::AssertionSuccess();
  }

  const envoy::config::core::v3::PathConfigSource path_;
  Stats::IsolatedStoreImpl stats_store_;
  SubscriptionStats stats_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Filesystem::Watcher::OnChangedCb on_changed_cb_;
  NiceMock<Config::MockSubscriptionCallbacks> callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  FilesystemCollectionSubscriptionImpl subscription_;
};

// Validate that an initial collection load succeeds, followed by a successful update, for inline
// entries.
TEST_F(FilesystemCollectionSubscriptionImplTest, InlineEntrySuccess) {
  TestUtility::TestOpaqueResourceDecoderImpl<envoy::config::listener::v3::Listener>
      resource_decoder("name");
  subscription_.start({});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, ""));
  // Initial config load.
  const auto inline_entry =
      TestUtility::parseYaml<xds::core::v3::CollectionEntry::InlineEntry>(R"EOF(
name: foo
version: resource.1
resource:
  "@type": type.googleapis.com/envoy.config.listener.v3.Listener
  name: foo
  address:
    socket_address:
      protocol: TCP
      address: 0.0.0.0
      port_value: 10000
  )EOF");
  const std::string resource =
      fmt::format(R"EOF(
version: system.1
resource:
  "@type": type.googleapis.com/envoy.config.listener.v3.ListenerCollection
  entries:
  - inline_entry: {}
  )EOF",
                  MessageUtil::getJsonStringFromMessageOrError(inline_entry));
  DecodedResourcesWrapper decoded_resources;
  decoded_resources.pushBack(std::make_unique<DecodedResourceImpl>(resource_decoder, inline_entry));
  EXPECT_CALL(callbacks_,
              onConfigUpdate(DecodedResourcesEq(decoded_resources.refvec_), "system.1"));
  updateFile(resource);
  EXPECT_TRUE(statsAre(2, 1, 0, 0, 1471442407191366964, "system.1"));
  // Update.
  const auto inline_entry_2 =
      TestUtility::parseYaml<xds::core::v3::CollectionEntry::InlineEntry>(R"EOF(
name: foo
version: resource.2
resource:
  "@type": type.googleapis.com/envoy.config.listener.v3.Listener
  name: foo
  address:
    socket_address:
      protocol: TCP
      address: 0.0.0.1
      port_value: 10001
  )EOF");
  const std::string resource_2 =
      fmt::format(R"EOF(
version: system.2
resource:
  "@type": type.googleapis.com/envoy.config.listener.v3.ListenerCollection
  entries:
  - inline_entry: {}
  )EOF",
                  MessageUtil::getJsonStringFromMessageOrError(inline_entry_2));
  {
    DecodedResourcesWrapper decoded_resources_2;
    decoded_resources_2.pushBack(
        std::make_unique<DecodedResourceImpl>(resource_decoder, inline_entry_2));
    EXPECT_CALL(callbacks_,
                onConfigUpdate(DecodedResourcesEq(decoded_resources_2.refvec_), "system.2"));
    updateFile(resource_2);
  }
  EXPECT_TRUE(statsAre(3, 2, 0, 0, 17889017004055064037ULL, "system.2"));
}

// Validate handling of invalid resource wrappers
TEST_F(FilesystemCollectionSubscriptionImplTest, BadEnvelope) {
  subscription_.start({});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, ""));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(ConfigUpdateFailureReason::UpdateRejected, _));
  // Unknown collection type.
  updateFile("{}");
  EXPECT_TRUE(statsAre(2, 0, 0, 1, 0, ""));
  const std::string resource = R"EOF(
version: system.1
resource:
  "@type": type.googleapis.com/envoy.config.listener.v3.Listener
  )EOF";
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(ConfigUpdateFailureReason::UpdateRejected, _));
  // Invalid collection type structure.
  updateFile(resource);
  EXPECT_TRUE(statsAre(3, 0, 0, 2, 0, ""));
}

// Validate handling of unknown fields.
TEST_F(FilesystemCollectionSubscriptionImplTest, UnknownFields) {
  subscription_.start({});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, ""));
  const std::string resource = R"EOF(
version: system.1
resource:
  "@type": type.googleapis.com/envoy.config.listener.v3.ListenerCollection
  entries:
  - inline_entry:
      name: foo
      version: resource.1
      resource:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        unknown_bar: baz
        address:
          socket_address:
            protocol: TCP
            address: 0.0.0.0
            port_value: 10000
  )EOF";
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(ConfigUpdateFailureReason::UpdateRejected, _));
  updateFile(resource);
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, ""));
}

// Validate handling of rejected config.
TEST_F(FilesystemCollectionSubscriptionImplTest, ConfigRejection) {
  subscription_.start({});
  EXPECT_TRUE(statsAre(1, 0, 0, 0, 0, ""));
  const std::string resource = R"EOF(
version: system.1
resource:
  "@type": type.googleapis.com/envoy.config.listener.v3.ListenerCollection
  entries:
  - inline_entry:
      name: foo
      version: resource.1
      resource:
        "@type": type.googleapis.com/envoy.config.listener.v3.Listener
        name: foo
        address:
          socket_address:
            protocol: TCP
            address: 0.0.0.0
            port_value: 10000
  )EOF";
  EXPECT_CALL(callbacks_, onConfigUpdate(_, _)).WillOnce(Throw(EnvoyException("blah")));
  EXPECT_CALL(callbacks_, onConfigUpdateFailed(ConfigUpdateFailureReason::UpdateRejected, _));
  updateFile(resource);
  EXPECT_TRUE(statsAre(2, 0, 1, 0, 0, ""));
}

} // namespace
} // namespace Config
} // namespace Envoy
