#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/common/wasm/remote_async_datasource.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

class AsyncDataSourceTest : public testing::Test {
protected:
  using AsyncDataSourcePb = envoy::config::core::v3::AsyncDataSource;

  NiceMock<Upstream::MockClusterManager> cm_;
  Init::MockManager init_manager_;
  Init::ExpectableWatcherImpl init_watcher_;
  Init::TargetHandlePtr init_target_handle_;
  Api::ApiPtr api_{Api::createApiForTest()};
  NiceMock<Random::MockRandomGenerator> random_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* retry_timer_;
  Event::TimerCb retry_timer_cb_;
  NiceMock<Http::MockAsyncClientRequest> request_{&cm_.thread_local_cluster_.async_client_};

  using AsyncClientSendFunc = std::function<Http::AsyncClient::Request*(
      Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
      const Http::AsyncClient::RequestOptions)>;

  void initialize(AsyncClientSendFunc func, int num_retries = 1) {
    retry_timer_ = new Event::MockTimer();
    EXPECT_CALL(init_manager_, add(_)).WillOnce(Invoke([this](const Init::Target& target) {
      init_target_handle_ = target.createHandle("test");
    }));

    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb timer_cb) {
      retry_timer_cb_ = timer_cb;
      return retry_timer_;
    }));

    EXPECT_CALL(*retry_timer_, disableTimer());
    if (!func) {
      return;
    }

    EXPECT_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .Times(AtLeast(1))
        .WillRepeatedly(ReturnRef(cm_.thread_local_cluster_.async_client_));

    if (num_retries == 1) {
      EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
          .Times(AtLeast(1))
          .WillRepeatedly(Invoke(func));
    } else {
      EXPECT_CALL(cm_.thread_local_cluster_.async_client_, send_(_, _, _))
          .Times(num_retries)
          .WillRepeatedly(Invoke(func));
    }
  }
};

} // namespace
} // namespace Envoy
