Fixed QUIC connection info to expose peer certificate details (digests, SANs, subject, issuer,
serial numbers, PEM encoding and validity dates). Previously all peer certificate accessors on
QUIC connections returned empty values, so for example access log and header formatters for the
peer certificate of upstream HTTP/3 connections produced empty output.
