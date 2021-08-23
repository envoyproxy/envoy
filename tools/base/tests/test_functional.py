import types
from unittest.mock import AsyncMock

import pytest

from tools.base import functional


@pytest.mark.asyncio
@pytest.mark.parametrize("cache", [None, True, False])
@pytest.mark.parametrize("raises", [True, False])
@pytest.mark.parametrize("result", [None, False, "X", 23])
async def test_functional_async_property(cache, raises, result):
    m_async = AsyncMock(return_value=result)

    class SomeError(Exception):
        pass

    if cache is None:
        decorator = functional.async_property
        iter_decorator = functional.async_property
    else:
        decorator = functional.async_property(cache=cache)
        iter_decorator = functional.async_property(cache=cache)

    items = [f"ITEM{i}" for i in range(0, 5)]

    class Klass:

        @decorator
        async def prop(self):
            """This prop deserves some docs"""
            if raises:
                await m_async()
                raise SomeError("AN ERROR OCCURRED")
            else:
                return await m_async()

        @iter_decorator
        async def iter_prop(self):
            """This prop also deserves some docs"""
            if raises:
                await m_async()
                raise SomeError("AN ITERATING ERROR OCCURRED")
            result = await m_async()
            for item in items:
                yield item, result

    klass = Klass()

    # The class.prop should be an instance of async_prop
    # and should have the name and docs of the wrapped method.
    assert isinstance(
        type(klass).prop,
        functional.async_property)
    assert (
        type(klass).prop.__doc__
        == "This prop deserves some docs")
    assert (
        type(klass).prop.name
        == "prop")

    if raises:
        with pytest.raises(SomeError) as e:
            await klass.prop

        with pytest.raises(SomeError) as e2:
            async for result in klass.iter_prop:
                pass

        assert (
            e.value.args[0]
            == 'AN ERROR OCCURRED')
        assert (
            e2.value.args[0]
            == 'AN ITERATING ERROR OCCURRED')
        assert (
            list(m_async.call_args_list)
            == [[(), {}]] * 2)
        return

    # results can be repeatedly awaited
    assert await klass.prop == result
    assert await klass.prop == result

    # results can also be repeatedly iterated
    results1 = []
    async for returned_result in klass.iter_prop:
        results1.append(returned_result)
    assert results1 == [(item, result) for item in items]

    results2 = []
    async for returned_result in klass.iter_prop:
        results2.append(returned_result)

    if not cache:
        assert results2 == results1
        assert (
            list(list(c) for c in m_async.call_args_list)
            == [[(), {}]] * 4)
        assert not hasattr(klass, functional.async_property.cache_name)
        return

    # with cache we can keep awaiting the result but the fun
    # is still only called once
    assert await klass.prop == result
    assert await klass.prop == result
    assert (
        list(list(c) for c in m_async.call_args_list)
        == [[(), {}]] * 2)

    iter_prop = getattr(klass, functional.async_property.cache_name)["iter_prop"]
    assert isinstance(iter_prop, types.AsyncGeneratorType)
    assert (
        getattr(klass, functional.async_property.cache_name)
        == dict(prop=m_async.return_value, iter_prop=iter_prop))

    # cached iterators dont give any more results once they are done
    assert results2 == []
