io_uring: added independent provided-buffer pool sizing, deferred task work, and receive
batching options. Multishot receives now pause under backpressure while preserving
peer-close notification and buffered data.
