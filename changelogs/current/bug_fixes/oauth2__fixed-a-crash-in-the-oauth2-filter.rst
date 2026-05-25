Fixed a crash in the OAuth2 filter where AES-CBC decryption of token cookies could spuriously
succeed (~1/256) when the configured HMAC secret did not match the secret used to encrypt the
cookie (for example after secret rotation, or when receiving legacy unencrypted tokens). The
resulting binary "plaintext" was written back into the ``Cookie:`` request header and tripped a
``HeaderString`` validation assert. Such plaintexts are now rejected and the original cookie value
is preserved, matching the behavior already documented for the explicit decryption-failure case.
