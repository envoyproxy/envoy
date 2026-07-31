Fixed a 32-bit integer overflow in the ``thrift_proxy`` lax (non-strict) binary protocol decoder.
A message name length of 0xFFFFFFF7 or greater wrapped the insufficient-data check in
``readMessageBegin`` and raised a spurious decode error that closed the downstream connection. The
check is now performed in 64-bit arithmetic and the decoder waits for more data instead, matching
the strict binary protocol.
