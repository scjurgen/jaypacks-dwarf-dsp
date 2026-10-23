"""The colours of the token kinds, per theme. No Qt here: the HTML page of the API uses them too."""

from __future__ import annotations

# kind: (colour, bold, italic); each colour keeps at least 4.5:1 against the editor background of its theme
LIGHT = {
    "comment": ("#5f6b76", False, True),
    "string": ("#0a7a3a", False, False),
    "number": ("#a65400", False, False),
    "keyword": ("#0033b3", True, False),
    "command": ("#7b1fa2", True, False),
    "hook": ("#0b6e99", True, False),
    "enum": ("#8e24aa", False, False),
    "enum_member": ("#8e24aa", False, True),
    "runtime": ("#00796b", False, False),
    "library": ("#00796b", True, False),
    "member": ("#00796b", False, False),
    "std": ("#455a64", False, False),
    "field": ("#1565c0", False, False),
}
DARK = {
    "comment": ("#8b949e", False, True),
    "string": ("#98c379", False, False),
    "number": ("#d19a66", False, False),
    "keyword": ("#569cd6", True, False),
    "command": ("#c586c0", True, False),
    "hook": ("#4fc1ff", True, False),
    "enum": ("#d7a1e8", False, False),
    "enum_member": ("#d7a1e8", False, True),
    "runtime": ("#4ec9b0", False, False),
    "library": ("#4ec9b0", True, False),
    "member": ("#4ec9b0", False, False),
    "std": ("#9cb2bd", False, False),
    "field": ("#9cdcfe", False, False),
}
