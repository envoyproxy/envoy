#pragma once

#include "envoy/server/options.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/thread.h"

#include "server/server.h"
#include "server/test_hooks.h"

namespace Server {

/**
 * Integration test options.
 */
class TestOptionsImpl : public Options {
public:
  TestOptionsImpl(const std::string& config_path) : config_path_(config_path) {}

  // Server::Options
  uint64_t baseId() override { return 0; }
  uint32_t concurrency() override { return 1; }
  const std::string& configPath() override { return config_path_; }
  std::chrono::seconds drainTime() override { return std::chrono::seconds(0); }
  spdlog::level::level_enum logLevel() override { NOT_IMPLEMENTED; }
  std::chrono::seconds parentShutdownTime() override { return std::chrono::seconds(0); }
  uint64_t restartEpoch() override { return 0; }
  std::chrono::milliseconds fileFlushIntervalMsec() override {
    return std::chrono::milliseconds(10000);
  }

private:
  const std::string config_path_;
};

class TestDrainManager : public DrainManager {
public:
  // Server::DrainManager
  bool drainClose() override { return draining_; }
  bool draining() override { return draining_; }
  void startDrainSequence() override {}
  void startParentShutdownSequence() override {}

  bool draining_{};
};

} // Server

class IntegrationTestServer;
typedef std::unique_ptr<IntegrationTestServer> IntegrationTestServerPtr;

/**
 * Wrapper for running the real server for the purpose of integration tests.
 */
class IntegrationTestServer : Logger::Loggable<Logger::Id::testing>,
                              public TestHooks,
                              public Server::ComponentFactory {
public:
  static IntegrationTestServerPtr create(const std::string& config_path);
  ~IntegrationTestServer();

  Server::TestDrainManager& drainManager() { return *drain_manager_; }
  Server::InstanceImpl& server() { return *server_; }
  void start();

  // TestHooks
  void onServerInitialized() override { server_initialized_.setReady(); }

  // Server::ComponentFactory
  Server::DrainManagerPtr createDrainManager(Server::Instance&) override {
    drain_manager_ = new Server::TestDrainManager();
    return Server::DrainManagerPtr{drain_manager_};
  }
  Runtime::LoaderPtr createRuntime(Server::Instance& server,
                                   Server::Configuration::Initial& config) override {
    return Server::InstanceUtil::createRuntime(server, config);
  }

protected:
  IntegrationTestServer(const std::string& config_path) : config_path_(config_path) {}

private:
  /**
   * Runs the real server on a thread.
   */
  void threadRoutine();

  const std::string config_path_;
  Thread::ThreadPtr thread_;
  Thread::ConditionalInitializer server_initialized_;
  std::unique_ptr<Server::InstanceImpl> server_;
  Server::TestDrainManager* drain_manager_{};
};
