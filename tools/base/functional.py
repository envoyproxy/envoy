#
# Functional utilities
#

import inspect
from typing import Any, Callable, Optional


class NoCache(Exception):
    pass


class async_property:  # noqa: N801
    name = None
    cache_name = "__async_prop_cache__"
    _instance = None

    # If the decorator is called with `kwargs` then `fun` is `None`
    # and instead `__call__` is triggered with `fun`
    def __init__(self, fun: Optional[Callable] = None, cache: bool = False):
        self.cache = cache
        self._fun = fun
        self.name = getattr(fun, "__name__", None)
        self.__doc__ = getattr(fun, '__doc__')

    def __call__(self, fun: Callable) -> 'async_property':
        self._fun = fun
        self.name = self.name or fun.__name__
        self.__doc__ = getattr(fun, '__doc__')
        return self

    def __get__(self, instance: Any, cls=None) -> Any:
        if instance is None:
            return self
        self._instance = instance
        if inspect.isasyncgenfunction(self._fun):
            return self.async_iter_result()
        return self.async_result()

    def fun(self, *args, **kwargs):
        if self._fun:
            return self._fun(*args, **kwargs)

    @property
    def prop_cache(self) -> dict:
        return getattr(self._instance, self.cache_name, {})

    # An async wrapper function to return the result
    # This is returned when the prop is called if the wrapped
    # method is an async generator
    async def async_iter_result(self):
        # retrieve the value from cache if available
        try:
            result = self.get_cached_prop()
        except (NoCache, KeyError):
            result = None

        if result is None:
            result = self.set_prop_cache(self.fun(self._instance))

        async for item in result:
            yield item

    # An async wrapper function to return the result
    # This is returned when the prop is called
    async def async_result(self) -> Any:
        # retrieve the value from cache if available
        try:
            return self.get_cached_prop()
        except (NoCache, KeyError):
            pass

        # derive the result, set the cache if required, and return the result
        return self.set_prop_cache(await self.fun(self._instance))

    def get_cached_prop(self) -> Any:
        if not self.cache:
            raise NoCache
        return self.prop_cache[self.name]

    def set_prop_cache(self, result: Any) -> Any:
        if not self.cache:
            return result
        cache = self.prop_cache
        cache[self.name] = result
        setattr(self._instance, self.cache_name, cache)
        return result
