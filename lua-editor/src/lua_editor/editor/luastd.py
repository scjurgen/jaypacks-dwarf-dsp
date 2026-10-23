"""The names of Lua 5.4 that the plugins' sandbox may offer: keywords and the base, math, string and table libraries."""

from __future__ import annotations

KEYWORDS = frozenset(
    "and break do else elseif end false for function goto if in local nil not or repeat return then true until while".split()
)

BASE = frozenset(
    "assert error getmetatable ipairs next pairs pcall rawequal rawget rawlen rawset select setmetatable tonumber "
    "tostring type xpcall _G _VERSION".split()
)

LIBRARIES: dict[str, frozenset[str]] = {
    "math": frozenset(
        "abs acos asin atan ceil cos deg exp floor fmod huge log max maxinteger min mininteger modf pi rad random "
        "randomseed sin sqrt tan tointeger type ult".split()
    ),
    "string": frozenset(
        "byte char dump find format gmatch gsub len lower match pack packsize rep reverse sub unpack upper".split()
    ),
    "table": frozenset("concat insert move pack remove sort unpack".split()),
}
