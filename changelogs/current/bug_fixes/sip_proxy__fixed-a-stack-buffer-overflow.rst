Fixed a stack buffer overflow in the SIP decoder. The width of the ``Content-Length`` header value
field is controlled by the peer and was copied into a fixed-size stack buffer via a ``copyOut()``
call bounded only by the source buffer, so an over-wide value field overflowed the stack. The
decoder now validates the value length against the destination size before copying and drops
messages with an empty, over-long, or negative ``Content-Length`` value instead of decoding them.
