#include "source/extensions/filters/http/ext_proc/allowed_override_modes_set.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;

class AllowedOverrideModesSetTest : public testing::Test {
protected:
  // Helper to create a ProcessingMode with specific settings for readability
  ProcessingMode createMode(ProcessingMode::HeaderSendMode req_header,
                            ProcessingMode::HeaderSendMode resp_header,
                            ProcessingMode::BodySendMode req_body,
                            ProcessingMode::BodySendMode resp_body,
                            ProcessingMode::HeaderSendMode req_trailer,
                            ProcessingMode::HeaderSendMode resp_trailer) {
    ProcessingMode pm;
    pm.set_request_header_mode(req_header);
    pm.set_response_header_mode(resp_header);
    pm.set_request_body_mode(req_body);
    pm.set_response_body_mode(resp_body);
    pm.set_request_trailer_mode(req_trailer);
    pm.set_response_trailer_mode(resp_trailer);
    return pm;
  }
};

// Verify exact matches work as expected.
TEST_F(AllowedOverrideModesSetTest, BasicExactMatch) {
  const ProcessingMode allowed =
      createMode(ProcessingMode::SEND, ProcessingMode::SKIP, ProcessingMode::BUFFERED,
                 ProcessingMode::NONE, ProcessingMode::SKIP, ProcessingMode::SEND);

  const std::vector<ProcessingMode> config_modes = {allowed};
  const AllowedOverrideModesSet set(config_modes);

  EXPECT_TRUE(set.isModeSupported(allowed));
}

// Verify that 'request_header_mode' is IGNORED during comparison.
// The allowed mode has request_header_mode = SEND.
// The candidate mode has request_header_mode = SKIP.
// All other fields match. Expected result: Supported.
TEST_F(AllowedOverrideModesSetTest, IgnoresRequestHeaderMode) {
  const ProcessingMode allowed =
      createMode(ProcessingMode::SEND, // Value 1.
                 ProcessingMode::SKIP, ProcessingMode::BUFFERED, ProcessingMode::NONE,
                 ProcessingMode::SKIP, ProcessingMode::SEND);

  const ProcessingMode candidate =
      createMode(ProcessingMode::SKIP, // Value 2 (Different!).
                 ProcessingMode::SKIP, ProcessingMode::BUFFERED, ProcessingMode::NONE,
                 ProcessingMode::SKIP, ProcessingMode::SEND);

  const std::vector<ProcessingMode> config_modes = {allowed};
  const AllowedOverrideModesSet set(config_modes);

  EXPECT_TRUE(set.isModeSupported(candidate));
}

// Verify that differences in other fields (e.g. response_body_mode) result in rejection.
TEST_F(AllowedOverrideModesSetTest, EnforcesOtherFields) {
  const ProcessingMode allowed =
      createMode(ProcessingMode::SEND, ProcessingMode::SKIP, ProcessingMode::BUFFERED,
                 ProcessingMode::NONE, ProcessingMode::SKIP, ProcessingMode::SEND);

  // Candidate differs in response_body_mode (STREAMED vs NONE)
  const ProcessingMode candidate =
      createMode(ProcessingMode::SEND, ProcessingMode::SKIP, ProcessingMode::BUFFERED,
                 ProcessingMode::STREAMED, ProcessingMode::SKIP, ProcessingMode::SEND);

  const std::vector<ProcessingMode> config_modes = {allowed};
  const AllowedOverrideModesSet set(config_modes);

  EXPECT_FALSE(set.isModeSupported(candidate));
}

// Verify behavior with multiple allowed modes.
TEST_F(AllowedOverrideModesSetTest, MultipleAllowedModes) {
  const ProcessingMode mode1 =
      createMode(ProcessingMode::SEND, ProcessingMode::SEND, ProcessingMode::NONE,
                 ProcessingMode::NONE, ProcessingMode::SKIP, ProcessingMode::SKIP);

  const ProcessingMode mode2 =
      createMode(ProcessingMode::SKIP, ProcessingMode::SKIP, ProcessingMode::BUFFERED,
                 ProcessingMode::BUFFERED, ProcessingMode::SEND, ProcessingMode::SEND);

  const std::vector<ProcessingMode> config_modes = {mode1, mode2};
  const AllowedOverrideModesSet set(config_modes);

  EXPECT_TRUE(set.isModeSupported(mode1));
  EXPECT_TRUE(set.isModeSupported(mode2));

  // A mix of mode1 and mode2 should NOT be supported
  const ProcessingMode mixed =
      createMode(ProcessingMode::SEND, ProcessingMode::SEND,         // Matches mode1
                 ProcessingMode::BUFFERED, ProcessingMode::BUFFERED, // Matches mode2
                 ProcessingMode::SKIP, ProcessingMode::SKIP);
  EXPECT_FALSE(set.isModeSupported(mixed));
}

// Verify behavior with an empty set.
TEST_F(AllowedOverrideModesSetTest, EmptySetReturnsFalse) {
  const std::vector<ProcessingMode> config_modes = {}; // Empty
  const AllowedOverrideModesSet set(config_modes);

  const ProcessingMode candidate =
      createMode(ProcessingMode::SEND, ProcessingMode::SEND, ProcessingMode::NONE,
                 ProcessingMode::NONE, ProcessingMode::SKIP, ProcessingMode::SKIP);

  EXPECT_TRUE(set.empty());
  EXPECT_FALSE(set.isModeSupported(candidate));
}
} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
