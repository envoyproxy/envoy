/**
 * Envoy documentation behaviour.
 *
 * Loaded as a module, so it is deferred. Anything that must run before the
 * first paint — currently only the stored colour theme — lives in
 * envoy-theme.js, which is loaded as a classic script ahead of this one.
 *
 * Every module below is optional: each one looks for the markup it needs and
 * returns if it is not on the page, so a build without the custom layout still
 * renders.
 */

import * as code from './envoy/code.js';
import * as lists from './envoy/lists.js';
import * as nav from './envoy/nav.js';
import * as proto from './envoy/proto.js';
import * as search from './envoy/search.js';
import * as theme from './envoy/theme.js';
import * as toc from './envoy/toc.js';
import * as versions from './envoy/versions.js';

// proto runs after toc: the outline links are rewritten there, and the kind
// dots are prepended to whatever is left.
const MODULES = [theme, nav, versions, code, lists, toc, proto, search];

function start() {
  MODULES.forEach((module) => {
    try {
      module.init();
    } catch (error) {
      // One broken component must not take the rest of the page with it.
      window.console.error('envoy docs:', error);
    }
  });
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', start);
} else {
  start();
}
