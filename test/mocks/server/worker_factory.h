#pragma once

#include "envoy/server/worker.h"

#include "gmock/gmock.h"
#include "worker.h"

namespace Envoy {
namespace Server {
class MockWorkerFactory : public WorkerFactory {
public:
  MockWorkerFactory();
  ~MockWorkerFactory() override;

  // Server::WorkerFactory
  WorkerPtr createWorker(OverloadManager&, const std::string&) override {
    return WorkerPtr{createWorker_()};
  }

  MOCK_METHOD(Worker*, createWorker_, ());
};
} // namespace Server
} // namespace Envoy
