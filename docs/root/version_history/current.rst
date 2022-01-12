1.22.0 (pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*


Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*


Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* data plane: fixing error handling where writing to a socket failed while under the stack of processing. This should genreally affect HTTP/3. This behavioral change can be reverted by setting ``envoy.reloadable_features.allow_upstream_inline_write`` to false.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`


New Features
------------


Deprecated
----------
