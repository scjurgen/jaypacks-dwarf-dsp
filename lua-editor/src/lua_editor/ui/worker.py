"""Runs RemoteFS calls on one background thread so the window never waits for the device."""

from __future__ import annotations

from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from typing import Any

from PySide6.QtCore import QObject, Signal, Slot


class RemoteWorker(QObject):
    _finished = Signal(object)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        # one thread: backends such as an ssh session are not safe for concurrent use
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="remote")
        self._finished.connect(self._deliver)

    def submit(
        self,
        job: Callable[[], Any],
        on_done: Callable[[Any], None],
        on_error: Callable[[Exception], None],
    ) -> None:
        """Run job in the background; on_done(result) or on_error(exception) is then called in the UI thread."""

        def run() -> None:
            try:
                outcome: tuple[Any, ...] = (on_done, job(), None, on_error)
            except Exception as error:  # reported to the caller, never lost
                outcome = (on_done, None, error, on_error)
            self._finished.emit(outcome)

        self._executor.submit(run)

    def call(self, job: Callable[[], Any]) -> Any:
        """Run job after everything queued so far and wait for it; the UI thread blocks, so keep it for closing."""
        return self._executor.submit(job).result()

    def shutdown(self) -> None:
        self._executor.shutdown(wait=True)

    @Slot(object)
    def _deliver(self, outcome: tuple[Any, ...]) -> None:
        on_done, result, error, on_error = outcome
        if error is not None:
            on_error(error)
        else:
            on_done(result)
