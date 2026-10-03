Fixed a crash (SIGSEGV) in load stats reporting and health discovery responses when EDS removed
the last host from a locality after active health checking failed. Both reporting paths now skip
empty locality groups.
