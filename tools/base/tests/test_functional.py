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
    else:
        decorator = functional.async_property(cache=cache)

    class Klass:

        @decorator
        async def prop(self):
            """This prop deserves some docs"""
            if raises:
                await m_async()
                raise SomeError("AN ERROR OCCURRED")
            else:
                return await m_async()

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

        assert (
            e.value.args[0]
            == 'AN ERROR OCCURRED')
        assert (
            list(m_async.call_args)
            == [(), {}])
        return

    # results can be repeatedly awaited
    assert await klass.prop == result
    assert await klass.prop == result

    if not cache:
        assert (
            list(list(c) for c in m_async.call_args_list)
            == [[(), {}], [(), {}]])
        assert not hasattr(klass, functional.async_property.cache_name)
        return

    # with cache we can keep awaiting the result but the fun
    # is still only called once
    assert await klass.prop == result
    assert await klass.prop == result
    assert (
        list(list(c) for c in m_async.call_args_list)
        == [[(), {}]])
    assert (
        getattr(klass, functional.async_property.cache_name)
        == dict(prop=m_async.return_value))
