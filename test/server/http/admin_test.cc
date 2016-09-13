#include "server/http/admin.h"

#include "test/mocks/server/mocks.h"

using testing::_;
using testing::NiceMock;

namespace Server {

class AdminFilterTest : public testing::Test {
public:
  // TODO: Switch to mocks and do not bind to a real port.
  AdminFilterTest()
      : admin_("/dev/null", 9002, server_), filter_(admin_), request_headers_{{":path", "/"}} {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  NiceMock<MockInstance> server_;
  AdminImpl admin_;
  AdminFilter filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::HeaderMapImpl request_headers_;
};

TEST_F(AdminFilterTest, HeaderOnly) {
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeHeaders(request_headers_, true);
}

TEST_F(AdminFilterTest, Body) {
  filter_.decodeHeaders(request_headers_, false);
  Buffer::OwnedImpl data("hello");
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeData(data, true);
}

TEST_F(AdminFilterTest, Trailers) {
  filter_.decodeHeaders(request_headers_, false);
  Buffer::OwnedImpl data("hello");
  filter_.decodeData(data, false);
  EXPECT_CALL(callbacks_, encodeHeaders_(_, false));
  filter_.decodeTrailers(request_headers_);
}

} // Server
