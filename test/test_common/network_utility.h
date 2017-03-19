#pragma once

#include "envoy/network/address.h"

namespace Network {
namespace Test {

/**
 * Determines if the passed in address and port is available for binding. If the port is zero,
 * the OS should pick an unused port for the supplied address (e.g. for the loopback address).
 * @param addrPort a valid host address (e.g. an address of one of the network interfaces
 *        of this host, or the any address or the loopback address) and port (zero to indicate
 *        that the OS should pick an unused address.
 * @param type the type of socket to be tested.
 * @returns the address and port (selected if zero was the passed in port) that can be used for
 *          listening, else nullptr if the address and port are not free.
 */
Address::InstancePtr checkPortAvailability(Address::InstancePtr addrPort, Address::SocketType type);

} // Test
} // Network
