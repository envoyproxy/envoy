#include "contrib/generic_proxy/filters/network/test/mocks/codec.h"

#include <memory>

using testing::ByMove;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

MockCodecFactory::MockCodecFactory() {
  ON_CALL(*this, createServerCodec())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockServerCodec>>())));
  ON_CALL(*this, createClientCodec())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockClientCodec>>())));
}

MockProxyFactory::MockProxyFactory() = default;

MockStreamCodecFactoryConfig::MockStreamCodecFactoryConfig() = default;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
