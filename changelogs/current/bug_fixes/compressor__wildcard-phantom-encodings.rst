Fixed a bug in the compressor filter where an ``accept-encoding`` header in which ``identity`` or ``*``
tied on q-value with another encoding inserted a phantom entry into the set of allowed compressors. A
subsequently winning wildcard could then resolve to the phantom entry instead of the first registered
compressor, leaving the response uncompressed even though a matching compressor was configured, and
miscounting the ``header_wildcard`` stat.
