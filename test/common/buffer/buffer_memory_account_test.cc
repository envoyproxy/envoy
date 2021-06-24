#include "envoy/http/codec.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/integration/tracked_watermark_buffer.h"
#include "test/mocks/http/stream_reset_handler.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

using testing::_;

using MemoryClassesToAccountsSet = std::array<absl::flat_hash_set<BufferMemoryAccountSharedPtr>, 8>;

constexpr uint64_t kMinimumBalanceToTrack = 1024 * 1024;
constexpr uint64_t kThresholdForFinalBucket = 128 * 1024 * 1024;

class BufferMemoryAccountTest : public testing::Test {
public:
  static void NoAccountsTracked(MemoryClassesToAccountsSet& memory_classes_to_account) {
    for (const auto& set : memory_classes_to_account) {
      EXPECT_TRUE(set.empty());
    }
  }

protected:
  TrackedWatermarkBufferFactory factory_;
  Http::MockStreamResetHandler mock_reset_handler_;
};

TEST_F(BufferMemoryAccountTest, ManagesAccountBalance) {
  auto account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer(account);
  ASSERT_EQ(account->balance(), 0);

  // Check the balance increases as expected.
  {
    // New slice created
    buffer.add("Hello");
    EXPECT_EQ(account->balance(), 4096);

    // Should just be added to existing slice.
    buffer.add(" World!");
    EXPECT_EQ(account->balance(), 4096);

    // Trigger new slice creation with add.
    const std::string long_string(4096, 'a');
    buffer.add(long_string);
    EXPECT_EQ(account->balance(), 8192);

    // AppendForTest also adds new slice.
    buffer.appendSliceForTest("Extra Slice");
    EXPECT_EQ(account->balance(), 12288);
  }

  // Check the balance drains as slices are consumed.
  {
    // Shouldn't trigger slice free yet
    buffer.drain(4095);
    EXPECT_EQ(account->balance(), 12288);

    // Trigger slice reclaim.
    buffer.drain(1);
    EXPECT_EQ(account->balance(), 8192);

    // Reclaim next slice
    buffer.drain(std::string("Hello World!").length());
    EXPECT_EQ(account->balance(), 4096);

    // Reclaim remaining
    buffer.drain(std::string("Extra Slice").length());
    EXPECT_EQ(account->balance(), 0);
  }
}

TEST_F(BufferMemoryAccountTest, BufferAccountsForUnownedSliceMovedInto) {
  auto account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl accounted_buffer(account);

  Buffer::OwnedImpl unowned_buffer;
  unowned_buffer.add("Unaccounted Slice");
  ASSERT_EQ(account->balance(), 0);

  // Transfer over buffer
  accounted_buffer.move(unowned_buffer);
  EXPECT_EQ(account->balance(), 4096);

  accounted_buffer.drain(accounted_buffer.length());
  EXPECT_EQ(account->balance(), 0);
}

TEST_F(BufferMemoryAccountTest, BufferFragmentsShouldNotHaveAnAssociatedAccount) {
  auto buffer_one_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_one(buffer_one_account);
  ASSERT_EQ(buffer_one_account->balance(), 0);

  auto buffer_two_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_two(buffer_two_account);
  ASSERT_EQ(buffer_two_account->balance(), 0);

  const char data[] = "hello world";
  BufferFragmentImpl frag(data, 11, nullptr);
  buffer_one.addBufferFragment(frag);
  EXPECT_EQ(buffer_one_account->balance(), 0);
  EXPECT_EQ(buffer_one.length(), 11);

  // Transfer over buffer
  buffer_two.move(buffer_one);
  EXPECT_EQ(buffer_two_account->balance(), 0);
  EXPECT_EQ(buffer_two.length(), 11);

  buffer_two.drain(buffer_two.length());
  EXPECT_EQ(buffer_two_account->balance(), 0);
  EXPECT_EQ(buffer_two.length(), 0);
}

TEST_F(BufferMemoryAccountTest, SliceRemainsAttachToOriginalAccountWhenMoved) {
  auto buffer_one_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_one(buffer_one_account);
  ASSERT_EQ(buffer_one_account->balance(), 0);

  auto buffer_two_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_two(buffer_two_account);
  ASSERT_EQ(buffer_two_account->balance(), 0);

  buffer_one.add("Charged to Account One");
  EXPECT_EQ(buffer_one_account->balance(), 4096);
  EXPECT_EQ(buffer_two_account->balance(), 0);

  // Transfer over buffer, still tied to account one.
  buffer_two.move(buffer_one);
  EXPECT_EQ(buffer_one_account->balance(), 4096);
  EXPECT_EQ(buffer_two_account->balance(), 0);

  buffer_two.drain(buffer_two.length());
  EXPECT_EQ(buffer_one_account->balance(), 0);
  EXPECT_EQ(buffer_two_account->balance(), 0);
}

TEST_F(BufferMemoryAccountTest,
       SliceRemainsAttachToOriginalAccountWhenMovedUnlessCoalescedIntoExistingSlice) {
  auto buffer_one_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_one(buffer_one_account);
  ASSERT_EQ(buffer_one_account->balance(), 0);

  auto buffer_two_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_two(buffer_two_account);
  ASSERT_EQ(buffer_two_account->balance(), 0);

  buffer_one.add("Will Coalesce");
  buffer_two.add("To be Coalesce into:");
  EXPECT_EQ(buffer_one_account->balance(), 4096);
  EXPECT_EQ(buffer_two_account->balance(), 4096);

  // Transfer over buffer, slices coalesce, crediting account one.
  buffer_two.move(buffer_one);
  EXPECT_EQ(buffer_one_account->balance(), 0);
  EXPECT_EQ(buffer_two_account->balance(), 4096);

  buffer_two.drain(std::string("To be Coalesce into:Will Coalesce").length());
  EXPECT_EQ(buffer_one_account->balance(), 0);
  EXPECT_EQ(buffer_two_account->balance(), 0);
}

TEST_F(BufferMemoryAccountTest, SliceCanRemainAttachedToOriginalAccountWhenMovedAndCoalescedInto) {
  auto buffer_one_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_one(buffer_one_account);
  ASSERT_EQ(buffer_one_account->balance(), 0);

  auto buffer_two_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_two(buffer_two_account);
  ASSERT_EQ(buffer_two_account->balance(), 0);

  auto buffer_three_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_three(buffer_three_account);
  ASSERT_EQ(buffer_three_account->balance(), 0);

  buffer_one.add("Will Coalesce");
  buffer_two.add("To be Coalesce into:");
  EXPECT_EQ(buffer_one_account->balance(), 4096);
  EXPECT_EQ(buffer_two_account->balance(), 4096);

  // Transfer buffers, leading to slice coalescing in third buffer.
  buffer_three.move(buffer_two);
  buffer_three.move(buffer_one);
  EXPECT_EQ(buffer_one_account->balance(), 0);
  EXPECT_EQ(buffer_two_account->balance(), 4096);
  EXPECT_EQ(buffer_three_account->balance(), 0);

  buffer_three.drain(std::string("To be Coalesce into:Will Coalesce").length());
  EXPECT_EQ(buffer_two_account->balance(), 0);
}

TEST_F(BufferMemoryAccountTest, LinearizedBufferShouldChargeItsAssociatedAccount) {
  auto buffer_one_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_one(buffer_one_account);
  ASSERT_EQ(buffer_one_account->balance(), 0);

  auto buffer_two_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_two(buffer_two_account);
  ASSERT_EQ(buffer_two_account->balance(), 0);

  auto buffer_three_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_three(buffer_three_account);
  ASSERT_EQ(buffer_three_account->balance(), 0);

  const std::string long_string(4096, 'a');
  buffer_one.add(long_string);
  buffer_two.add(long_string);
  EXPECT_EQ(buffer_one_account->balance(), 4096);
  EXPECT_EQ(buffer_two_account->balance(), 4096);

  // Move into the third buffer.
  buffer_three.move(buffer_one);
  buffer_three.move(buffer_two);
  EXPECT_EQ(buffer_one_account->balance(), 4096);
  EXPECT_EQ(buffer_two_account->balance(), 4096);
  EXPECT_EQ(buffer_three_account->balance(), 0);

  // Linearize, which does a copy out of the slices.
  buffer_three.linearize(8192);
  EXPECT_EQ(buffer_one_account->balance(), 0);
  EXPECT_EQ(buffer_two_account->balance(), 0);
  EXPECT_EQ(buffer_three_account->balance(), 8192);
}

TEST_F(BufferMemoryAccountTest, ManagesAccountBalanceWhenPrepending) {
  auto prepend_to_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_to_prepend_to(prepend_to_account);
  ASSERT_EQ(prepend_to_account->balance(), 0);

  auto prepend_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer_to_prepend(prepend_account);
  ASSERT_EQ(prepend_account->balance(), 0);

  Buffer::OwnedImpl unowned_buffer_to_prepend;

  unowned_buffer_to_prepend.add("World");
  buffer_to_prepend.add("Goodbye World");
  EXPECT_EQ(prepend_account->balance(), 4096);

  // Prepend the buffers.
  buffer_to_prepend_to.prepend(buffer_to_prepend);
  EXPECT_EQ(prepend_account->balance(), 4096);
  EXPECT_EQ(prepend_to_account->balance(), 0);

  buffer_to_prepend_to.prepend(unowned_buffer_to_prepend);
  EXPECT_EQ(prepend_to_account->balance(), 4096);

  // Prepend a string view.
  buffer_to_prepend_to.prepend("Hello ");
  EXPECT_EQ(prepend_to_account->balance(), 8192);
}

TEST_F(BufferMemoryAccountTest, ExtractingSliceWithExistingStorageCreditsAccountOnce) {
  auto buffer_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer(buffer_account);
  ASSERT_EQ(buffer_account->balance(), 0);

  buffer.appendSliceForTest("Slice 1");
  buffer.appendSliceForTest("Slice 2");
  EXPECT_EQ(buffer_account->balance(), 8192);

  // Account should only be credited when slice is extracted.
  // Not on slice dtor.
  {
    auto slice = buffer.extractMutableFrontSlice();
    EXPECT_EQ(buffer_account->balance(), 4096);
  }

  EXPECT_EQ(buffer_account->balance(), 4096);
}

TEST_F(BufferMemoryAccountTest, NewReservationSlicesOnlyChargedAfterCommit) {
  auto buffer_account = factory_.createAccount(&mock_reset_handler_);
  Buffer::OwnedImpl buffer(buffer_account);
  ASSERT_EQ(buffer_account->balance(), 0);

  auto reservation = buffer.reserveForRead();
  EXPECT_EQ(buffer_account->balance(), 0);

  // We should only be charged for the slices committed.
  reservation.commit(16384);
  EXPECT_EQ(buffer_account->balance(), 16384);
}

TEST_F(BufferMemoryAccountTest, ReservationShouldNotChargeForExistingSlice) {
  auto buffer_account = factory_.createAccount(&mock_reset_handler_);

  Buffer::OwnedImpl buffer(buffer_account);
  ASSERT_EQ(buffer_account->balance(), 0);

  buffer.add("Many bytes remaining in this slice to use for reservation.");
  EXPECT_EQ(buffer_account->balance(), 4096);

  // The account shouldn't be charged again at commit since the commit
  // uses memory from the slice already charged for.
  auto reservation = buffer.reserveForRead();
  reservation.commit(2000);
  EXPECT_EQ(buffer_account->balance(), 4096);
}

TEST_F(BufferMemoryAccountTest, AccountShouldNotBeTrackedByFactoryUnlessAboveMinimumBalance) {
  auto account = factory_.createAccount(&mock_reset_handler_);

  // Check not tracked
  factory_.inspectMemoryClasses(BufferMemoryAccountTest::NoAccountsTracked);

  // Still below minimum
  account->charge(2020);
  factory_.inspectMemoryClasses(BufferMemoryAccountTest::NoAccountsTracked);

  account->charge(kMinimumBalanceToTrack);

  // Check now tracked
  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[0].size(), 1);
  });

  account->credit(account->balance());
}

TEST_F(BufferMemoryAccountTest, ClearingDownstreamShouldUnregisterTrackedAccounts) {
  auto account = factory_.createAccount(&mock_reset_handler_);
  account->charge(kMinimumBalanceToTrack);

  // Check tracked
  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[0].size(), 1);
  });

  account->clearDownstream();

  // Check no longer tracked
  factory_.inspectMemoryClasses(BufferMemoryAccountTest::NoAccountsTracked);

  account->credit(account->balance());
}

TEST_F(BufferMemoryAccountTest, AccountCanResetStream) {
  auto account = factory_.createAccount(&mock_reset_handler_);

  EXPECT_CALL(mock_reset_handler_, resetStream(_));
  account->resetDownstream(Http::StreamResetReason::LocalReset);
}

TEST_F(BufferMemoryAccountTest, FactoryTracksAccountCorrectlyAsBalanceIncreases) {
  auto account = factory_.createAccount(&mock_reset_handler_);
  account->charge(kMinimumBalanceToTrack);

  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[0].size(), 1);
  });

  // TODO(kbaichoo): avoid magic numbers (size of set - 1)
  for (int i = 0; i < 8 - 1; ++i) {
    // Double the balance to enter the higher buckets.
    account->charge(account->balance());
    factory_.inspectMemoryClasses([i](MemoryClassesToAccountsSet& memory_classes_to_account) {
      EXPECT_EQ(memory_classes_to_account[i].size(), 0);
      EXPECT_EQ(memory_classes_to_account[i + 1].size(), 1);
    });
  }

  account->credit(account->balance());
}

TEST_F(BufferMemoryAccountTest, FactoryTracksAccountCorrectlyAsBalanceDecreases) {
  auto account = factory_.createAccount(&mock_reset_handler_);
  account->charge(kThresholdForFinalBucket);

  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[7].size(), 1);
  });

  // TODO(kbaichoo): UN magic number this.
  const int second_largest_index = 6;

  for (int i = second_largest_index; i > 0; --i) {
    // Halve the balance to enter the lower buckets.
    account->credit(account->balance() / 2);
    factory_.inspectMemoryClasses([i](MemoryClassesToAccountsSet& memory_classes_to_account) {
      EXPECT_EQ(memory_classes_to_account[i + 1].size(), 0);
      EXPECT_EQ(memory_classes_to_account[i].size(), 1);
    });
  }

  account->credit(account->balance());
}

TEST_F(BufferMemoryAccountTest, SizeSaturatesInLargestBucket) {
  auto account = factory_.createAccount(&mock_reset_handler_);
  account->charge(kThresholdForFinalBucket);

  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[7].size(), 1);
  });

  account->charge(account->balance());

  // Remains in final bucket.
  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[7].size(), 1);
  });

  account->credit(account->balance());
}

TEST_F(BufferMemoryAccountTest, RemainsInSameBucketIfChangesWithinThreshold) {
  auto account = factory_.createAccount(&mock_reset_handler_);
  account->charge(kMinimumBalanceToTrack);

  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[0].size(), 1);
  });

  // Charge to see in same bucket.
  account->charge(kMinimumBalanceToTrack - 1);

  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[0].size(), 1);
  });

  // Credit to see in same bucket.
  account->credit(kMinimumBalanceToTrack - 1);

  factory_.inspectMemoryClasses([](MemoryClassesToAccountsSet& memory_classes_to_account) {
    EXPECT_EQ(memory_classes_to_account[0].size(), 1);
  });

  account->credit(account->balance());
}

} // namespace
} // namespace Buffer
} // namespace Envoy
