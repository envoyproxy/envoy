#pragma once

#include "extensions/filters/http/ext_proc/client.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class MockClient : public ExternalProcessorClient {
public:
  MockClient();
  virtual ~MockClient();
  MOCK_METHOD(ExternalProcessorStreamPtr, start,
              (ExternalProcessorCallbacks&, const std::chrono::milliseconds&));
};

class MockStream : public ExternalProcessorStream {
public:
  MockStream();
  virtual ~MockStream();
  MOCK_METHOD(void, send, (envoy::service::ext_proc::v3alpha::ProcessingRequest&&, bool));
  MOCK_METHOD(void, close, ());
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy