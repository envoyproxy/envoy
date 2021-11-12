#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/network/win32_redirect_records_option_impl.h"

#include "test/common/network/socket_option_test.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

class Win32RedirectRecordsOptionImplTest : public SocketOptionTest {

public:
  Win32RedirectRecordsOptionImplTest() {
    redirect_records_data_ = "some data";
    redirect_records_ = std::make_shared<Network::Win32RedirectRecords>();
    memcpy(redirect_records_->buf_, reinterpret_cast<void*>(redirect_records_data_.data()),
           redirect_records_data_.size());
    redirect_records_->buf_size_ = redirect_records_data_.size();
  }

protected:
  void SetUp() override {
    EXPECT_CALL(os_sys_calls_, socket)
        .WillRepeatedly(Invoke([this](int domain, int type, int protocol) {
          return os_sys_calls_actual_.socket(domain, type, protocol);
        }));
    EXPECT_CALL(os_sys_calls_, close(_)).Times(testing::AnyNumber());
  }

  std::string redirect_records_data_;
  std::shared_ptr<Network::Win32RedirectRecords> redirect_records_;
};

TEST_F(Win32RedirectRecordsOptionImplTest, IgnoresOptionOnDifferentState) {

  Win32RedirectRecordsOptionImpl socket_option{*redirect_records_};
  EXPECT_TRUE(
      socket_option.setOption(socket_, envoy::config::core::v3::SocketOption::STATE_LISTENING));
}

TEST_F(Win32RedirectRecordsOptionImplTest, FailsOnSyscallFailure) {
  EXPECT_CALL(socket_, ioctl(_, _, _, _, _, _))
      .WillRepeatedly(testing::Return(Api::SysCallIntResult{-1, SOCKET_ERROR_NOT_SUP}));
  Win32RedirectRecordsOptionImpl socket_option{*redirect_records_};
  EXPECT_FALSE(
      socket_option.setOption(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND));
}

TEST_F(Win32RedirectRecordsOptionImplTest, SetOption) {
  Win32RedirectRecordsOptionImpl socket_option{*redirect_records_};
  EXPECT_TRUE(
      socket_option.setOption(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND));
}

TEST_F(Win32RedirectRecordsOptionImplTest, IsSupported) {
  Win32RedirectRecordsOptionImpl socket_option{*redirect_records_};
#ifdef SIO_SET_WFP_CONNECTION_REDIRECT_RECORDS
  EXPECT_TRUE(socket_option.isSupported());
#else
  EXPECT_FALSE(socket_option.isSupported());
#endif
}

TEST_F(Win32RedirectRecordsOptionImplTest, HashKey) {
  std::vector<uint8_t> hash;
  Win32RedirectRecordsOptionImpl socket_option{*redirect_records_};
  socket_option.hashKey(hash);
  std::vector<uint8_t> expected_hash;
  pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(redirect_records_data_), expected_hash);
  EXPECT_EQ(hash, expected_hash);
}

TEST_F(Win32RedirectRecordsOptionImplTest, HashKeyDifferent) {
  std::vector<uint8_t> hash;
  Win32RedirectRecordsOptionImpl socket_option{*redirect_records_};
  socket_option.hashKey(hash);

  std::string other_redirect_records_data = "some other data";
  auto other_redirect_records = std::make_shared<Network::Win32RedirectRecords>();
  memcpy(other_redirect_records->buf_, reinterpret_cast<void*>(other_redirect_records_data.data()),
         other_redirect_records_data.size());
  other_redirect_records->buf_size_ = other_redirect_records_data.size();

  std::vector<uint8_t> other_hash;
  Win32RedirectRecordsOptionImpl other_socket_option{*other_redirect_records};
  other_socket_option.hashKey(other_hash);

  EXPECT_NE(hash, other_hash);
}

TEST_F(Win32RedirectRecordsOptionImplTest, OptionDetails) {
  const auto state = envoy::config::core::v3::SocketOption::STATE_PREBIND;
  Socket::Option::Details expected_details{Win32RedirectRecordsOptionImpl::optionName(),
                                           redirect_records_data_};
  Win32RedirectRecordsOptionImpl socket_option{*redirect_records_};
  EXPECT_EQ(expected_details, socket_option.getOptionDetails(socket_, state));
}

} // namespace
} // namespace Network
} // namespace Envoy
