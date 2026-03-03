"""Lightweight container for data received from an Envoy stream."""

import json
from typing import Any, Dict, List, Optional, Union

from library.python.envoy_engine import EnvoyError


class Response:
    def __init__(self) -> None:
        self.body_raw = bytearray()
        self.status_code: Optional[int] = None
        self.headers: Dict[str, Union[str, List[str]]] = {}
        self.trailers: Dict[str, Union[str, List[str]]] = {}
        self.envoy_error: Optional[EnvoyError] = None

    @property
    def body(self) -> bytes:
        return bytes(self.body_raw)

    @property
    def text(self) -> str:
        # TODO: respect charset from headers
        return str(self.body, "utf8")

    def json(self) -> Dict[str, Any]:
        return json.loads(self.body)
