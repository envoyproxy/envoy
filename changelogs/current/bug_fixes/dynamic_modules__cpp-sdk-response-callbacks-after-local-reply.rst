dynamic modules: fixed a bug in the C++ SDK where a filter stopped receiving
``onResponseHeaders``, ``onResponseBody`` and ``onResponseTrailers`` after any local reply on the
stream, including local replies the module did not send. A module could not observe or modify the
response for a ``direct_response`` route, a local reply from another filter, or an
Envoy-generated error. The Rust and Go SDKs were unaffected.
