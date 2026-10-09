Fixed an out-of-bounds read in the Redis inline-command decoder when parsing a quoted string whose
hex escape (``\xNN``) appears at the very start of the token.
