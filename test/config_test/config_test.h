#pragma once

namespace ConfigTest {

/**
 * Load all configurations from a tar file and make sure they are valid. Config files are expected
 * to exist in $TEST_TMPDIR/config_test.
 * @return uint32_t the number of configs tested.
 */
uint32_t run();

} // ConfigTest
