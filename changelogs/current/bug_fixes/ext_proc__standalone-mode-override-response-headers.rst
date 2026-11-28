ext_proc: fixed a bug where receiving a standalone mode override message during response header
processing incorrectly validated the decoding state instead of the encoding state, causing spurious
message errors and stream termination.
