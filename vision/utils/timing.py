"""Method timing without changing return values."""

from functools import wraps
import time


def timed(attribute: str):
    """Store the last call's wall-clock milliseconds on the method's instance."""
    def decorate(method):
        @wraps(method)
        def wrapper(self, *args, **kwargs):
            start = time.perf_counter()
            try:
                return method(self, *args, **kwargs)
            finally:
                setattr(self, attribute, (time.perf_counter() - start) * 1000)

        return wrapper
    return decorate
