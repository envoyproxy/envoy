Fix: `CVE-2026-47775 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-396h-jpq4-vc7p>`_

Addressed a padding oracle in the OAuth2 filter's AES-256-CBC cookie decryption. The filter
now supports AES-256-GCM encryption with a ``gcm.`` algorithm marker, which authenticates the
ciphertext and removes the oracle.

**The fix is opt-in to keep rolling upgrades safe.** On upgrade, the default behaviour is
unchanged: cookies are still encrypted with AES-256-CBC and the CBC decrypt path is still
reachable, so existing sessions and mixed-version clusters keep working. Two runtime flags
control the migration:

* ``envoy.reloadable_features.oauth2_use_gcm_encryption`` (default ``false``) — when set to
  ``true``, ``encrypt()`` produces AES-256-GCM ciphertexts prefixed with ``gcm.``. While
  ``false`` (the default), ``encrypt()`` continues to emit AES-256-CBC ciphertexts with no
  prefix, wire-compatible with older instances.
* ``envoy.reloadable_features.oauth2_legacy_cbc_decrypt_compat`` (default ``true``) — when
  ``true``, ``decrypt()`` accepts both ``gcm.``-prefixed cookies (via GCM) and legacy
  cookies (via the legacy CBC fallback). When set to ``false``, only ``gcm.``-prefixed
  cookies decrypt, legacy CBC cookies are rejected and the affected users are redirected
  to the OAuth server to re-authenticate. While the CBC fallback is reachable, it partially
  reopens CVE-2026-47775.

You should set ``envoy.reloadable_features.oauth2_use_gcm_encryption`` to ``true`` once
you have ensured that all instances in your cluster are capable of decrypting GCM-encrypted cookies.
And then, you could set ``envoy.reloadable_features.oauth2_legacy_cbc_decrypt_compat`` to ``false``
to disable the legacy CBC decryption path at appropriate time.

**Never set ``envoy.reloadable_features.oauth2_legacy_cbc_decrypt_compat`` to ``false``
before you have enabled ``envoy.reloadable_features.oauth2_use_gcm_encryption``.**

Both flags and the AES-256-CBC code paths are scheduled for removal once the migration
window has elapsed.

The OAuth2 filter exposes a new counter ``oauth_legacy_cbc_decrypt`` that increments each
time a cookie is successfully decrypted via the legacy CBC fallback. Operators should watch
this stat decay to zero across the migration window before flipping
``oauth2_legacy_cbc_decrypt_compat`` to ``false``.
