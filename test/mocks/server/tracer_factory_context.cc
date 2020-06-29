#include "tracer_factory_context.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Server {
namespace Configuration {
MockTracerFactoryContext::MockTracerFactoryContext() {
  ON_CALL(*this, serverFactoryContext()).WillByDefault(ReturnRef(server_factory_context_));
  ON_CALL(*this, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));
}


MockTracerFactoryContext::~MockTracerFactoryContext() = default;



}

}

}
