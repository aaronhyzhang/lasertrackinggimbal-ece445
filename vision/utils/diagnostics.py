"""Flushed runtime diagnostics and native crash traces for tracking sessions."""
import faulthandler
import json
import logging
import os
from pathlib import Path
import platform
import threading
import time
from datetime import datetime

import psutil
import torch


class Diagnostics:
    def __init__(self, path=None, interval=2.0):
        self.path = Path(path) if path else Path(__file__).resolve().parents[1] / "logs" / (
            datetime.now().strftime("run-%Y%m%d-%H%M%S-%f") + f"-{os.getpid()}.log")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.handler = logging.FileHandler(self.path, encoding="utf-8")
        self.handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
        self.log = logging.Logger(f"vision.diagnostics.{id(self)}", logging.DEBUG)
        self.log.addHandler(self.handler)
        self.interval = interval
        self.state = {"stage": "startup", "frame": 0}
        self.stage_since = time.monotonic()
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.process = psutil.Process()
        self.process.cpu_percent()
        psutil.cpu_percent()
        self.native = self.path.with_suffix(".fault.log").open("a")
        self.owns_fault_handler = not faulthandler.is_enabled()
        if self.owns_fault_handler:
            faulthandler.enable(file=self.native, all_threads=True)
        self.log.info("START pid=%s python=%s platform=%s torch=%s", os.getpid(),
                      platform.python_version(), platform.platform(), torch.__version__)
        self.thread = threading.Thread(target=self._monitor, name="runtime-diagnostics", daemon=True)
        self.thread.start()

    def mark(self, stage, **values):
        with self.lock:
            self.state.update(values, stage=stage)
            self.stage_since = time.monotonic()

    def sample(self):
        with self.lock:
            data = dict(self.state, stage_age_s=round(time.monotonic() - self.stage_since, 3))
        memory = psutil.virtual_memory()
        data.update(process_cpu_pct=self.process.cpu_percent(), system_cpu_pct=psutil.cpu_percent(),
                    rss_mib=round(self.process.memory_info().rss / 2**20, 1),
                    available_ram_mib=round(memory.available / 2**20, 1), ram_pct=memory.percent,
                    threads=self.process.num_threads())
        if torch.backends.mps.is_available():
            # Allocator counters, not GPU utilization; do not synchronize inference.
            for name in ("current_allocated_memory", "driver_allocated_memory", "recommended_max_memory"):
                metric = getattr(torch.mps, name, None)
                if metric is not None:
                    try:
                        data[f"mps_{name}_mib"] = round(metric() / 2**20, 1)
                    except Exception as error:
                        data[f"mps_{name}_error"] = str(error)
        self.log.info("METRICS %s", json.dumps(data, default=str))

    def _monitor(self):
        while not self.stop.is_set():
            try:
                self.sample()
            except Exception:
                self.log.exception("Metrics sampling failed")
            self.stop.wait(self.interval)

    def close(self):
        self.stop.set()
        self.thread.join(timeout=3)
        self.log.info("END diagnostics closed")
        if self.owns_fault_handler:
            faulthandler.disable()
        self.native.close()
        # A stuck backend sampler must not prevent the main thread from exiting.
        if not self.thread.is_alive():
            self.handler.close()
