#include <gtest/gtest.h>

extern "C" {

extern void run_tests_c();
}

TEST(TestHttpParser, TestStockImplementation) { run_tests_c(); }
