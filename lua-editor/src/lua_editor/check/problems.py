"""What a check reports about a script."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Problem:
    line: int  # 1-based
    column: int | None  # 1-based; None when only the line is known
    length: int  # characters to mark from the column (the whole line when the column is None)
    message: str
    severity: str = "error"  # error or warning
    source: str = "syntax"
