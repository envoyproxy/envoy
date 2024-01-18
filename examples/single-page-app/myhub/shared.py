import json
import logging
import pathlib
from functools import cached_property
from typing import Optional

import yaml

logger = logging.getLogger(__name__)


def debug_request(request, resource):
    logger.debug(
        f"Received {resource} request:\n  {request.query}\n  {request.cookies}\n  {request.match_info}"
    )


class Data:

    def __init__(self, path: pathlib.Path) -> None:
        self._path = path

    def __contains__(self, k: str) -> bool:
        return k in self._data

    def __getitem__(self, k: str) -> str:
        return self._data.__getitem__(k)

    def __str__(self) -> str:
        return json.dumps(self._data)

    @property
    def _data(self) -> dict:
        return yaml.safe_load(self._storage.open())

    @cached_property
    def _storage(self) -> pathlib.Path:
        return self._path

    def get(self, k: str, default: Optional[str] = None) -> str:
        data = self._data
        return data.get(k, default)


class TokenStorage(Data):

    @property
    def _data(self):
        return json.load(self._storage.open())

    @cached_property
    def _storage(self):
        if not self._path.exists():
            self._path.parent.mkdir(exist_ok=True, parents=True)
            self._path.write_text('{}')
        return self._path

    def __setitem__(self, k: str, v: str) -> None:
        data = self._data
        data[k] = v
        json.dump(data, self._storage.open("w"))

    def pop(self, k: str) -> str:
        data = self._data
        result = data.pop(k)
        json.dump(data, self._storage.open("w"))
        return result
