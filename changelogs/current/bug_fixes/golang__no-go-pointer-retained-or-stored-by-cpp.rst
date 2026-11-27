Fixed the Go plugin interface breaking the cgo pointer rules. When copying all request headers or
trailers into Go, C++ wrote pointers into Go's buffer into the Go string array, which bypasses the
Go write barrier; this affected the HTTP filter, the HTTP-TCP bridge and the cluster specifier.
C++ now writes only the lengths and Go sets the data pointers. When a call has to wait for the
worker thread, C++ kept the addresses of Go variables and wrote through them after the cgo call had
returned. This affected the C API calls behind ``GetFilterState`` in the network filter, and behind
``DynamicMetadata().Get``, ``FilterState().GetString``, ``GetProperty`` and
``SecretManager().GetGenericSecret`` in the HTTP filter. Those variables are now pinned with
``runtime.Pinner`` until the write is done, and the property path is copied before the work is
posted. Because C++ no longer fills in the string data pointers, a Go plugin built against an older
SDK crashes on its first header copy when run with this Envoy; rebuild plugins with the matching
SDK version.
