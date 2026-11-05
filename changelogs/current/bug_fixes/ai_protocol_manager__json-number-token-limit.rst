Fixed the streaming JSON parser used by ``ai_protocol_manager`` so the 64-byte number length limit
is enforced when a number finishes parsing, rather than only when an incomplete number at the end
of a chunk exceeds 64 bytes.
