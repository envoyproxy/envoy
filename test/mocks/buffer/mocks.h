#pragma once

#include "common/buffer/buffer_impl.h"

#include "test/test_common/utility.h"

MATCHER_P(BufferEqual, rhs, testing::PrintToString(*rhs)) {
  return TestUtility::buffersEqual(arg, *rhs);
}

MATCHER_P(BufferStringEqual, rhs, rhs) {
  *result_listener << "\"" << TestUtility::bufferToString(arg) << "\"";

  Buffer::OwnedImpl buffer(rhs);
  return TestUtility::buffersEqual(arg, buffer);
}

ACTION_P(AddBufferToString, target_string) {
  target_string->append(TestUtility::bufferToString(arg0));
  arg0.drain(arg0.length());
}
