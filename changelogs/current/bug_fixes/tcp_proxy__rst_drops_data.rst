Fixed a bug in tcp_proxy where an upstream TCP RST was propagated to downstream even when the
HTTP stream had already completed, causing buffered response data to be dropped and the
client to receive EOF instead of the response. The RST is now only propagated when
the stream was not already complete.
