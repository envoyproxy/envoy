Fixed the Go HTTP filter panicking, and so replying with a 500, when ``DynamicMetadata().Set`` was
given a string that is not valid UTF-8, such as a value copied from a request header. Invalid
bytes are now replaced with U+FFFD. String values are also no longer round-tripped through a
serialized ``google.protobuf.Value``.
