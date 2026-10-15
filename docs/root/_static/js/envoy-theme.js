/**
 * Applies the stored colour theme before the document paints.
 *
 * This runs as a classic script in <head> rather than as part of the module
 * bundle, because modules are deferred and the reader would see a flash of the
 * wrong theme. Everything else lives in js/envoy.js.
 */
(function () {
  'use strict';

  var KEY = 'envoy-docs-theme';
  var stored = null;

  try {
    stored = window.localStorage.getItem(KEY);
  } catch (error) {
    // Private browsing and blocked storage both land here; fall back to the
    // system preference.
  }

  if (stored !== 'light' && stored !== 'dark') {
    // No explicit choice: leave the attribute off so the prefers-color-scheme
    // rules apply and keep following the system.
    return;
  }

  document.documentElement.dataset.envoyTheme = stored;
  document.documentElement.style.colorScheme = stored;
})();
