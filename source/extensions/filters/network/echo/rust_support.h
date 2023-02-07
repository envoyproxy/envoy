#include "envoy/buffer/buffer.h"
#include "envoy/network/filter.h"
#include "rust/cxx.h"
#include "envoy/network/connection.h"

Envoy::Network::Connection& connection(Envoy::Network::ReadFilterCallbacks& callbacks);

void write(Envoy::Network::Connection& connection, Envoy::Buffer::Instance& data, bool end_stream);
