http: fixed a crash when a local reply is sent on a stream whose decoding was deferred to the next
I/O cycle or whose upgrade request was rejected while an encoder filter pauses the response, by
aborting the decoder filter chain and clearing deferred request state so trailing request frames do
not enter an uninitialized or aborted decoder filter chain.

