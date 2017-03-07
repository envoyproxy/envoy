#pragma once

namespace Network {
namespace Test {

/**
 * @returns an unused port, either be getting one from a port server, or by searching
 * through the ports for an unused port.
 */
int getUnusedPort();

} // Test
} // Network
