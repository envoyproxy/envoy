The ``ConfigValidatorFactory::typeUrl()`` method is deprecated in favor of
``ConfigValidator::typeUrl()``, which resolves the xDS type url from the validator instance and can
therefore depend on the validator's configuration. This only affects extension code, not
configuration. ``ConfigValidatorFactory::typeUrl()`` is no longer pure virtual: it now defaults to
returning an empty value, and ``ConfigValidator::typeUrl()`` likewise defaults to empty. Envoy calls
``ConfigValidator::typeUrl()`` first and falls back to the deprecated
``ConfigValidatorFactory::typeUrl()`` only when it returns empty, so a validator only needs to
implement the new instance method. The deprecated method keeps working and will be removed once the
in-tree and out-of-tree extensions have migrated. Note that Envoy itself builds with
``-Wno-deprecated-declarations``, so this deprecation is only visible to out-of-tree builds that
enable the warning.
