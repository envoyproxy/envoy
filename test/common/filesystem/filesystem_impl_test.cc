#include <chrono>
#include <string>

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::_;
using testing::ByMove;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::Throw;

namespace Envoy {
namespace Filesystem {

class FileSystemImplTest : public TestBase {
protected:
  FileSystemImplTest()
      : raw_file_(new NiceMock<MockRawFile>),
        file_system_(std::chrono::milliseconds(10000), Thread::threadFactoryForTest(), stats_store_,
                     raw_instance_) {
    EXPECT_CALL(raw_instance_, createRawFile(_))
        .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<MockRawFile>>(raw_file_))));
  }

  NiceMock<MockRawFile>* raw_file_;
  const std::chrono::milliseconds timeout_40ms_{40};
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<MockRawInstance> raw_instance_;
  InstanceImpl file_system_;
};

} // namespace Filesystem
} // namespace Envoy
