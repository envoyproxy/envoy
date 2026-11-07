Fixed the ALTS transport socket dropping the peer's end of stream when it arrived together with the
peer's last handshake message. The end of stream is now reported once the handshake completes.
