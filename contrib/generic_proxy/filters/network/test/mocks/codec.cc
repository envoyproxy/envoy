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
  ON_CALL(*this, requestDecoder())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockRequestDecoder>>())));
  ON_CALL(*this, responseDecoder())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockResponseDecoder>>())));
  ON_CALL(*this, requestEncoder())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockRequestEncoder>>())));
  ON_CALL(*this, responseEncoder())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockResponseEncoder>>())));
  ON_CALL(*this, messageCreator())
      .WillByDefault(Return(ByMove(std::make_unique<NiceMock<MockMessageCreator>>())));
}

MockProxyFactory::MockProxyFactory() = default;

MockStreamCodecFactoryConfig::MockStreamCodecFactoryConfig() = default;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
