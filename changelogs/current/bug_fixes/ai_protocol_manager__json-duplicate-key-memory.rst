Reduced the memory used by the streaming JSON parser in ``ai_protocol_manager`` for duplicate-key
detection. Keys of small objects are now kept packed in one buffer and compared linearly, and only
objects with many keys fall back to a hash set, so a body made of many tiny objects no longer costs
tens of bytes of bookkeeping per one-byte key. Duplicate keys are rejected exactly as before.
