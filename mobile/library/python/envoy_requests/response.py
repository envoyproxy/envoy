import json
from typing import Any
from typing import Dict
from typing import List
from typing import Optional
from typing import Union

import envoy_engine


class Response:
    def __init__(self):
        self.body_raw = bytearray()
        self.status_code: Optional[int] = None
        self.headers: Dict[str, Union[str, List[str]]] = {}
        self.trailers: Dict[str, Union[str, List[str]]] = {}
        self.envoy_error: Optional[envoy_engine.EnvoyError] = None

    @property
    def body(self) -> bytes:
        return bytes(self.body_raw)

    @property
    def text(self) -> str:
        # TODO: use charset when provided
        return str(self.body, "utf8")

    def json(self) -> Dict[str, Any]:
        return json.loads(self.body)
