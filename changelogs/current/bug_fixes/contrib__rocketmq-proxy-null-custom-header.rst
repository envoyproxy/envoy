Fixed a crash in the ``rocketmq_proxy`` network filter (contrib) where a request whose custom
header failed to decode (for example a request frame with the RPC flag set, which routes decoding
to the response-header path) left the typed custom header null. The request handlers then
dereferenced the null header. The handlers now reject such requests with a system error response
instead of crashing.
