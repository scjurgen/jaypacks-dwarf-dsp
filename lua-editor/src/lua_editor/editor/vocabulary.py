"""The names a script of one plugin may use, put together from the plugin's API and from Lua itself."""

from __future__ import annotations

import re
from collections.abc import Sequence

from lua_editor.api.model import Api, Library
from lua_editor.editor import luastd

# the globals every plugin's runtime adds (scripting/ApiDescription.cpp); api.json lists their names, not their meaning
RUNTIME_DOCS = {
    "Log": "Log(...): writes a line to the script's log file",
    "print": "print(...): the same as Log",
    "clamp": "clamp(x, lo, hi): x limited to lo..hi",
    "lerp": "lerp(a, b, t): the value t of the way from a to b",
    "import": 'import "name": runs another script file of the plugin once (letters, digits, - and _)',
}
IMPORT = re.compile(r"""\bimport\s*\(?\s*["']([\w-]+)["']""")


class Vocabulary:
    def __init__(self, api: Api | None, extra_libraries: Sequence[Library] = ()) -> None:
        self.api = api
        self.commands = api.commands if api else {}
        self.hooks = api.hooks if api else {}
        self.enums = api.enums if api else {}
        # the libraries found next to the script win over the factory ones of the same name
        self.libraries: dict[str, Library] = {lib.global_name: lib for lib in (api.libraries if api else [])}
        self.libraries.update({lib.global_name: lib for lib in extra_libraries})
        sandbox = api.sandbox if api else {}
        self.runtime_globals: list[str] = list(sandbox.get("runtimeGlobals", []))
        # luastd.BASE has none of the removed functions; print is back as a runtime global
        self.base = sorted(luastd.BASE | set(self.runtime_globals))
        removed_fields = sandbox.get("removedFields", {})
        self.std_tables = {
            name: sorted(names - set(removed_fields.get(name, [])))
            for name, names in luastd.LIBRARIES.items()
            if not api or name in sandbox.get("libraries", [])
        }
        self.noop_imports: list[str] = list(api.noop_imports) if api else []
        self.field_names = {f.name for c in self.commands.values() for f in c.fields}

    def import_names(self) -> list[str]:
        """The names import "..." accepts: the libraries of the plugin, then the ones that do nothing."""
        return sorted({lib.import_name for lib in self.libraries.values()}) + self.noop_imports

    def imported_libraries(self, text: str) -> dict[str, Library]:
        """The libraries a script has loaded with import, by their global name."""
        names = set(IMPORT.findall(re.sub(r"--[^\n]*", "", text)))
        return {g: lib for g, lib in self.libraries.items() if lib.import_name in names}
