Fixed truncation of zstd-compressed bodies when a small input chunk completed a highly compressible block.
The decompressor now checks the cumulative expansion ratio across input chunks.
