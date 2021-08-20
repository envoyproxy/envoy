from abc import abstractmethod
from random import random
from unittest.mock import MagicMock

import pytest

from tools.base import abstract


def test_implementer_constructor():
    with pytest.raises(TypeError):
        abstract.Implementer()

    assert issubclass(abstract.Implementer, type)


@pytest.mark.parametrize("isinst", [True, False])
def test_implementer_abstract_info(patches, isinst):
    abstract_methods = [f"METHOD{i}" for i in range(0, 5)]
    abstract_class = MagicMock()
    abstract_class.__abstractmethods__ = abstract_methods
    abstract_class.__name__ = MagicMock()

    patched = patches(
        "isinstance",
        prefix="tools.base.abstract")

    with patched as (m_inst, ):
        m_inst.return_value = isinst
        if not isinst:
            with pytest.raises(TypeError) as e:
                abstract.Implementer.abstract_info(abstract_class)
            assert (
                e.value.args[0]
                == f"Implementers can only implement subclasses of `abstract.Abstraction`, unrecognized: '{abstract_class}'")
        else:
            assert (
                abstract.Implementer.abstract_info(abstract_class)
                == (f"{abstract_class.__module__}.{abstract_class.__name__}",
                    abstract_class.__doc__,
                    abstract_methods))


@pytest.mark.parametrize("methods", [[], [f"METHOD{i}" for i in range(0, 2)], [f"METHOD{i}" for i in range(0, 5)]])
@pytest.mark.parametrize("has_docs", [[], [f"METHOD{i}" for i in range(0, 2)], [f"METHOD{i}" for i in range(0, 5)]])
@pytest.mark.parametrize("is_classmethod", [[], [f"METHOD{i}" for i in range(0, 2)], [f"METHOD{i}" for i in range(0, 5)]])
@pytest.mark.parametrize("doc", [True, False])
def test_implementer_add_docs(patches, methods, has_docs, doc, is_classmethod):
    abstract_docs = {f"DOC{i}": int(random() * 10) for i in range(0, 5)}
    abstract_methods = {f"METHOD{i}": MagicMock() for i in range(0, 5)}

    clsdict = MagicMock()
    klass = MagicMock()

    if not doc:
        klass.__doc__ = ""

    for method, abstract_klass in abstract_methods.items():
        getattr(abstract_klass, method).__doc__ = "KLASS DOCS"
        if method not in methods:
            delattr(klass, method)
            continue
        if method not in has_docs:
            getattr(klass, method).__doc__ = ""
        if method in is_classmethod:
            getattr(klass, method).__self__ = "CLASSMETHOD_CLASS"

    patched = patches(
        "Implementer.implementation_info",
        prefix="tools.base.abstract")

    with patched as (m_info, ):
        m_info.return_value = abstract_docs, abstract_methods
        assert not abstract.Implementer.add_docs(clsdict, klass)

    assert (
        klass.__doc__
        == (MagicMock.__doc__
            if doc
            else "\n".join(f"Implements: {k}\n{v}\n" for k, v in abstract_docs.items())))

    failed = False
    for abstract_method, abstract_klass in abstract_methods.items():
        if not abstract_method in methods:
            continue
        expected = MagicMock.__doc__
        if abstract_method in is_classmethod and abstract_method not in has_docs:
            expected = ""
        elif abstract_method not in has_docs:
            expected = "KLASS DOCS"
        assert (
            getattr(klass, abstract_method).__doc__
            == expected)


@pytest.mark.parametrize("bases", [(), ("A", "B", "C"), ("A", "C")])
@pytest.mark.parametrize("implements", [(), ("A", "B", "C"), ("A", "C")])
def test_implementer_get_bases(bases, implements):
    clsdict = dict(__implements__=implements)
    assert (
        abstract.Implementer.get_bases(bases, clsdict)
        == bases + tuple(x for x in clsdict["__implements__"] if x not in bases))


@pytest.mark.parametrize("has_docs", ["odd", "even"])
def test_implementer_implementation_info(patches, has_docs):
    patched = patches(
        "Implementer.abstract_info",
        prefix="tools.base.abstract")

    def iter_implements():
        for x in range(0, 5):
            yield f"ABSTRACT{x}"

    implements = list(iter_implements())
    clsdict = dict(__implements__=implements)

    def abstract_info(abstract):
        x = int(abstract[-1])
        methods = [f"METHOD{i}" for i in range(0, x + 1)]
        docs = (
            x % 2
            if has_docs == "even"
            else not x % 2)
        return abstract, docs, methods

    with patched as (m_info, ):
        m_info.side_effect = abstract_info
        docs, methods = abstract.Implementer.implementation_info(clsdict)

    def oddeven(i):
        return (
            i % 2
            if has_docs == "even"
            else not i % 2)

    assert docs == {f"ABSTRACT{i}": 1 for i in range(0, 5) if oddeven(i)}
    assert (
        methods
        == {f'METHOD{i}': f'ABSTRACT{i}' for i in range(0, 5)})


@pytest.mark.parametrize("has_implements", [True, False])
def test_implementer_dunder_new(patches, has_implements):
    patched = patches(
        "super",
        "Implementer.add_docs",
        prefix="tools.base.abstract")
    bases = MagicMock()
    clsdict = dict(FOO="BAR")

    if has_implements:
        clsdict["__implements__"] = "IMPLEMENTS"

    class SubImplementer(abstract.Implementer):
        pass

    class Super:

        def __new__(cls, *args):
            if not args:
                return cls
            return "NEW"

    with patched as (m_super, m_docs):
        m_super.side_effect = Super
        assert (
            abstract.Implementer.__new__(abstract.Implementer, "NAME", bases, clsdict)
            == "NEW")

    if has_implements:
        assert (
            list(m_docs.call_args)
            == [(clsdict, "NEW"), {}])
    else:
        assert not m_docs.called


class AFoo(metaclass=abstract.Abstraction):

    @classmethod
    @abstractmethod
    def do_something_classy(cls):
        pass

    @abstractmethod
    def do_something(self):
        """Do something"""
        raise NotImplementedError


class ABar(metaclass=abstract.Abstraction):

    @abstractmethod
    def do_something_else(self):
        """Do something else"""
        raise NotImplementedError


class Baz:

    def do_nothing(self):
        pass


@pytest.mark.parametrize("implements", [(), AFoo, (AFoo, ), (AFoo, ABar), (AFoo, ABar, Baz), Baz])
def test_implementer_decorator(patches, implements):
    iterable_implements = (
        implements
        if isinstance(implements, tuple)
        else (implements, ))
    should_fail = any(
        not isinstance(impl, abstract.Abstraction)
        for impl
        in iterable_implements)

    if should_fail:
        with pytest.raises(TypeError):
            _implementer(implements)
        return

    implementer = _implementer(implements)
    for impl in iterable_implements:
        assert issubclass(implementer, impl)
    assert (
        implementer.__name__
        == implementer.__qualname__
        == "ImplementationOfAnImplementer")
    assert (
        implementer.__doc__
        == 'A test implementation of an implementer')


def _implementer(implements):

    @abstract.implementer(implements)
    class ImplementationOfAnImplementer:
        """A test implementation of an implementer"""

        @classmethod
        def do_something_classy(cls):
            pass

        def do_something(self):
            pass

        def do_something_else(self):
            pass

        def do_nothing(self):
            pass

    return ImplementationOfAnImplementer
