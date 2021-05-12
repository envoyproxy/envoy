from abc import ABC
from abc import abstractmethod
from typing import Any
from typing import Callable
from typing import TypeVar


Func = TypeVar("Func", bound=Callable[..., Any])


class Executor(ABC):
    # TODO: verify that this preserves type signature
    @abstractmethod
    def wrap(self, fn: Func) -> Func:
        pass
