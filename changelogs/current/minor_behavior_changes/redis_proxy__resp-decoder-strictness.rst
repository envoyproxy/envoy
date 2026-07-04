The Redis proxy codec is stricter about malformed RESP wire input that was previously accepted
silently: negative aggregate or bulk length headers other than the spec's ``*-1`` / ``$-1`` null
forms, integer lines carrying no digits, integers outside the signed 64-bit range, and messages
exceeding new nesting-depth, cumulative-element and scalar-token limits are now treated as protocol
errors that close the connection. Locally generated error replies additionally have ASCII control
bytes replaced with spaces so attacker-influenced text cannot inject RESP framing. These changes are
only visible to peers sending non-conforming or abusive wire data.
