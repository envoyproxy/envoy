import pathlib

__IMPORTS__  # noqa: F821

_loader = __LOADER__  # noqa: F821

data = _loader(pathlib.Path(__file__).parent.joinpath("__DATA_FILE__").read_text())

_filters = __FILTERS__  # noqa: F821

_filters = (_filters,) if not isinstance(_filters, tuple) else _filters

for _filter in _filters:
    data = _filter(data)

__all__ = ("data",)
