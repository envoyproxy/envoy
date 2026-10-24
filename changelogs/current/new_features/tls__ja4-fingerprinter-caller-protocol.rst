Added an optional ``protocol`` parameter to ``JA4Fingerprinter::create`` in the TLS inspector's
``JA4`` fingerprint library, allowing callers to specify the transport carrying the handshake
(TLS, QUIC, or DTLS) per the
`JA4 specification <https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md>`_. The
default is ``Protocol::TLS``, preserving prior behavior for existing callers, including the
built-in TLS inspector listener filter.
