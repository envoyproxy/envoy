Fixed TLS session ID generation so that every CA certificate in the trusted CA bundle, not
just the first one, contributes to the digest that keys resumable sessions. Previously a
change to any CA after the first -- for example rotating or removing a trust anchor through
an xDS update -- left previously issued session IDs valid, allowing a resumed session to be
accepted without validation against the updated trust bundle. Configurations with a single
CA produce byte-identical session IDs to the previous behavior; configurations with a
multi-certificate bundle will issue new session IDs once after the upgrade, causing a
one-time increase in full handshakes.
