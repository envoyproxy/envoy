#pragma once

#include "gtest/gtest.h"

#define EXPECT_CONTEXT_PARAMS(context_params, ...)                                                 \
  {                                                                                                \
    std::map<std::string, std::string> param_map((context_params).params().begin(),                \
                                                 (context_params).params().end());                 \
    EXPECT_THAT(param_map, ::testing::UnorderedElementsAre(__VA_ARGS__));                          \
  }

namespace Envoy {
namespace Config {
namespace {} // namespace
} // namespace Config
} // namespace Envoy
