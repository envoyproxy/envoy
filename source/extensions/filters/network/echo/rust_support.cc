#include "source/extensions/filters/network/echo/rust_support.h"

Envoy::Network::Connection& connection(Envoy::Network::ReadFilterCallbacks& callbacks) {
  return callbacks.connection();
}

void write(Envoy::Network::Connection& connection, Envoy::Buffer::Instance& data, bool end_stream) {
  connection.write(data, end_stream);
}
