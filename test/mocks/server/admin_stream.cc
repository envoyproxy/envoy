#include "admin_stream.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
MockAdminStream::MockAdminStream() = default;

MockAdminStream::~MockAdminStream() = default;

StrictMockAdminStream::StrictMockAdminStream() = default;

StrictMockAdminStream::~StrictMockAdminStream() = default;
} // namespace Server
} // namespace Envoy
