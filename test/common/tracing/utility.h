#pragma once

#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Tracing {
namespace {

#define CUSTOM_TAG_CASE(config, type, tag, ...)                                                    \
  config.custom_tags_.push_back(std::make_shared<type>(tag, ##__VA_ARGS__))
#define EXPECT_SET_CUSTOM_TAG(span, tag, expected_value, config, type, ...)                        \
  CUSTOM_TAG_CASE(config, type, tag, ##__VA_ARGS__);                                               \
  EXPECT_CALL(span, setTag(Eq(tag), Eq(expected_value)))
#define EXPECT_UNSET_CUSTOM_TAG(span, tag, config, type, ...)                                      \
  CUSTOM_TAG_CASE(config, type, tag, ##__VA_ARGS__);                                               \
  EXPECT_CALL(span, setTag(Eq(tag), _)).Times(0)

} // namespace
} // namespace Tracing
} // namespace Envoy
