import unittest
from unittest.mock import patch

from vision.utils.timing import timed


class Processor:
    @timed("elapsed_ms")
    def process(self, value, fail=False):
        """Return the supplied value unchanged."""
        if fail:
            raise ValueError("failed")
        return value


class TimingTests(unittest.TestCase):
    def test_elapsed_time_is_in_milliseconds(self):
        processor = Processor()
        value = object()
        with patch("vision.utils.timing.time.perf_counter", side_effect=[1., 1.025]):
            self.assertIs(processor.process(value), value)
        self.assertAlmostEqual(processor.elapsed_ms, 25.)
        self.assertEqual(processor.process.__name__, "process")
        self.assertEqual(processor.process.__doc__, "Return the supplied value unchanged.")

    def test_exception_is_preserved_and_elapsed_time_is_recorded(self):
        processor = Processor()
        with patch("vision.utils.timing.time.perf_counter", side_effect=[2., 2.01]):
            with self.assertRaisesRegex(ValueError, "failed"):
                processor.process(None, fail=True)
        self.assertAlmostEqual(processor.elapsed_ms, 10.)

    def test_repeated_calls_update_only_their_own_instance(self):
        first, second = Processor(), Processor()
        with patch("vision.utils.timing.time.perf_counter", side_effect=[1., 1.01, 2., 2.03]):
            first.process(1)
            second.process(2)
        self.assertAlmostEqual(first.elapsed_ms, 10.)
        self.assertAlmostEqual(second.elapsed_ms, 30.)
        with patch("vision.utils.timing.time.perf_counter", side_effect=[3., 3.05]):
            first.process(3)
        self.assertAlmostEqual(first.elapsed_ms, 50.)
        self.assertAlmostEqual(second.elapsed_ms, 30.)
