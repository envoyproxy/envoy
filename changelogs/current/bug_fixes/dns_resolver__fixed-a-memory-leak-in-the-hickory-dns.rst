Fixed a memory leak in the Hickory DNS resolver where ``ActiveDnsQuery::cancel()``
did not free the pending query state or decrement the ``pending_resolutions`` gauge. Cancelled
queries now release their shell object, Rust-side query box, and gauge tick synchronously,
matching the contract followed by the c-ares and Apple DNS resolvers.
