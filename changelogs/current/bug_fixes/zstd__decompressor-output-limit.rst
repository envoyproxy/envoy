Added ``max_inflate_ratio`` and ``max_block_size`` to the zstd decompressor. Setting the block allowance to the
sender's maximum uncompressed block size allows a small input chunk to complete a buffered block. Each call
still forwards the output step that crosses its threshold before returning.
