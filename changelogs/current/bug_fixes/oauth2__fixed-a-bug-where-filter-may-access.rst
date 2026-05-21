Fixed a bug where the asyncronous token change callback could be triggered after the filter had been
torn down (``onDestroy()`` had been called), which could lead to access dangling pointers and result
in UAF/crash.
