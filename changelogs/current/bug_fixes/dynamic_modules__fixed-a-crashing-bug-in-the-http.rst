Fixed a crashing bug in the HTTP filter when a stream was already above the downstream write-buffer
high watermark at filter-chain construction time. Downstream watermark callback registration is
now deferred until the in-module filter has been constructed.
