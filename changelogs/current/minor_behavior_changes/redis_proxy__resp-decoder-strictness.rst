The Redis proxy codec is stricter about malformed RESP wire input that was previously accepted
silently: negative aggregate or bulk length headers other than the spec's ``*-1`` / ``$-1`` null
forms, integer lines carrying no digits, integers outside the signed 64-bit range, and messages
exceeding new nesting-depth, cumulative-element, inline-command-element and scalar-token limits are
now treated as protocol errors that close the connection. A single bulk string, blob error or
verbatim string payload is additionally capped at 512 MiB, matching Redis's default
``proto-max-bulk-len``; deployments whose backends raise ``proto-max-bulk-len`` beyond that default
are affected by this cap. Locally generated error replies also have ASCII control bytes replaced
with spaces so attacker-influenced text cannot inject RESP framing. The remaining changes are only
visible to peers sending non-conforming or abusive wire data.
