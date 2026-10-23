"""The API of a plugin as one self-contained HTML page for reading in a browser.

python -m lua_editor.api.html <plugin> [--apis FOLDER] [--out FILE]   (without --out the page goes to stdout)
"""

from __future__ import annotations

import argparse
import json
import sys
from html import escape
from pathlib import Path

from lua_editor.api.model import Api, ApiRegistry, Command, Example, Hook, Library, Member, Value
from lua_editor.editor.lualex import NORMAL, tokenize_line
from lua_editor.editor.palette import DARK, LIGHT
from lua_editor.editor.vocabulary import Vocabulary

CSS = """
:root { --bg: #ffffff; --fg: #1d2330; --muted: #5d6677; --line: #d9dde5; --code: #f1f3f7; --accent: #0b5fb3; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #14171d; --fg: #dde2ea; --muted: #959eae; --line: #2c323d; --code: #1d222b; --accent: #6cb2f5; }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--fg); font: 15px/1.5 system-ui, sans-serif; }
nav { position: fixed; top: 0; left: 0; bottom: 0; width: 220px; overflow-y: auto; padding: 16px;
      border-right: 1px solid var(--line); font-size: 13px; }
nav h2 { font-size: 11px; text-transform: uppercase; letter-spacing: .06em; color: var(--muted); margin: 16px 0 4px; }
nav a { display: block; padding: 1px 0; }
main { margin-left: 220px; max-width: 900px; padding: 16px 32px 64px; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
h1 { margin: 8px 0 0; }
h2 { margin-top: 40px; border-bottom: 1px solid var(--line); padding-bottom: 4px; }
h3 { margin: 28px 0 4px; }
section:target > h3 { background: var(--code); }
code, pre { font: 13px/1.4 ui-monospace, Menlo, monospace; background: var(--code); border-radius: 3px; }
code { padding: 1px 4px; }
pre { padding: 8px 12px; overflow-x: auto; }
table { border-collapse: collapse; width: 100%; margin: 8px 0; }
th, td { text-align: left; vertical-align: top; padding: 4px 8px; border-bottom: 1px solid var(--line); }
th { font-size: 12px; color: var(--muted); font-weight: 600; }
td.name { white-space: nowrap; }
.muted { color: var(--muted); }
@media (max-width: 760px) { nav { position: static; width: auto; border: 0; } main { margin: 0; padding: 16px; } }
"""


def _token_css() -> str:
    def rules(palette: dict, prefix: str = "") -> str:
        return "".join(
            f"{prefix}.t-{kind} {{ color: {colour};{' font-weight: bold;' if bold else ''}"
            f"{' font-style: italic;' if italic else ''} }}\n"
            for kind, (colour, bold, italic) in palette.items()
        )

    return rules(LIGHT) + "@media (prefers-color-scheme: dark) {\n" + rules(DARK) + "}\n"


EXTRA_CSS = """
pre.code { position: relative; }
button.copy { position: absolute; top: 6px; right: 6px; font: 12px system-ui, sans-serif; cursor: pointer;
              border: 1px solid var(--line); background: var(--bg); color: var(--fg); border-radius: 3px; padding: 1px 8px; }
.usedin { font-size: 13px; color: var(--muted); margin: 4px 0 0; }
details { margin: 8px 0; }
summary { cursor: pointer; }
"""

COPY_SCRIPT = """<script>
document.addEventListener('click', function (event) {
  var button = event.target.closest('button.copy');
  if (!button) return;
  var text = button.parentNode.querySelector('code').textContent;
  var done = function () { button.textContent = 'Copied'; setTimeout(function () { button.textContent = 'Copy'; }, 1500); };
  if (navigator.clipboard) { navigator.clipboard.writeText(text).then(done); return; }
  var area = document.createElement('textarea');
  area.value = text; document.body.appendChild(area); area.select(); document.execCommand('copy');
  document.body.removeChild(area); done();
});
</script>"""

KIND_NAMES = {"function": "function", "table": "table", "value": "value"}


def command_anchor(name: str) -> str:
    return f"cmd-{name}"


def hook_anchor(name: str) -> str:
    return f"hook-{name}"


def enum_anchor(name: str) -> str:
    return f"enum-{name}"


def library_anchor(library: Library) -> str:
    return f"lib-{library.global_name}"


def anchor_for(api: Api, word: str) -> str | None:
    """The anchor of the page for a command, hook, enum or library global; None for any other word."""
    if word in api.commands:
        return command_anchor(word)
    if word in api.hooks:
        return hook_anchor(word)
    if word in api.enums:
        return enum_anchor(word)
    for library in api.libraries:
        if word in (library.global_name, library.import_name):
            return library_anchor(library)
    return None


def _e(text: object) -> str:
    return escape(str(text))


def example_anchor(example: Example) -> str:
    return f"ex-{example.id}"


def script_anchor(example: Example) -> str:
    return f"script-{example.id}"


def highlight(code: str, vocab: Vocabulary) -> str:
    """The code as HTML spans of the editor's token kinds."""
    out: list[str] = []
    state = NORMAL
    for line in code.split("\n"):
        tokens, state = tokenize_line(line, state, vocab)
        at = 0
        parts = []
        for token in tokens:
            parts.append(_e(line[at : token.start]))
            parts.append(f"<span class='t-{token.kind}'>{_e(line[token.start : token.end])}</span>")
            at = token.end
        parts.append(_e(line[at:]))
        out.append("".join(parts))
    return "\n".join(out)


def _code_block(code: str, vocab: Vocabulary) -> str:
    return f"<pre class='code'><button class='copy' type='button'>Copy</button><code>{highlight(code, vocab)}</code></pre>"


def _used_in(api: Api, name: str) -> str:
    """'Used in' line under a command, hook, enum or library: the examples that name it."""
    links = [f"<a href='#{example_anchor(e)}'>{_e(e.title)}</a>" for e in api.used_in(name)]
    links += [f"<a href='#{script_anchor(s)}'>{_e(s.id)}.lua</a>" for s in api.scripts_using(name)]
    return f"<p class='usedin'>Used in: {', '.join(links)}</p>" if links else ""


def _examples(api: Api, vocab: Vocabulary) -> str:
    def entry(example: Example, anchor: str) -> str:
        uses = ", ".join(
            f"<a href='#{a}'><code>{_e(name)}</code></a>" if (a := anchor_for(api, name)) else f"<code>{_e(name)}</code>"
            for name in example.uses
        )
        return (
            f"<section id='{anchor}'><h3>{_e(example.title)}</h3><p>{_e(example.summary)}</p>"
            f"<p class='muted'>File <code>{_e(example.file)}</code>. Uses: {uses}</p>{_code_block(example.code, vocab)}</section>"
        )

    parts = []
    if api.examples:
        parts.append("<h2 id='examples'>Examples</h2>")
        parts.append(
            "<p class='muted'>Each runs as it is: copy it to a user slot and select it. "
            "Every command, hook, constant and library above lists the examples that use it.</p>"
        )
        parts += [entry(e, example_anchor(e)) for e in api.examples]
    if api.scripts:
        parts.append("<h2 id='scripts'>Complete scripts</h2><p class='muted'>The factory scripts of the plugin.</p>")
        parts += [
            f"<details id='{script_anchor(s)}'><summary><strong>{_e(s.id)}.lua</strong>: {_e(s.title)}</summary>"
            f"<p>{_e(s.summary)}</p>{_code_block(s.code, vocab)}</details>"
            for s in api.scripts
        ]
    return "".join(parts)


def _values_table(values: list[Value], heading: str) -> str:
    rows = []
    for value in values:
        note = _e(value.doc)
        if value.values:
            items = "".join(f"<li><code>{_e(name)}</code> {_e(doc)}</li>" for name, doc in value.values)
            note += f"<ul>{items}</ul>"
        rows.append(f"<tr><td class='name'><code>{_e(value.name)}</code></td><td>{_e(value.describe())}</td><td>{note}</td></tr>")
    return f"<table><tr><th>{heading}</th><th>Type, range, default</th><th></th></tr>{''.join(rows)}</table>"


def _command(api: Api, command: Command) -> str:
    parts = [f"<section id='{command_anchor(command.name)}'><h3><code>{_e(command.signature())}</code></h3>"]
    parts.append(f"<p>{_e(command.doc)}</p>")
    if command.args:
        parts.append(_values_table(command.args, "Argument"))
    if command.fields:
        parts.append("<p class='muted'>Fields of the table argument, all optional:</p>")
        parts.append(_values_table(command.fields, "Field"))
    parts.append(_used_in(api, command.name))
    parts.append("</section>")
    return "".join(parts)


def _hook(api: Api, hook: Hook) -> str:
    return (
        f"<section id='{hook_anchor(hook.name)}'><h3><code>function {_e(hook.signature())}</code></h3>"
        f"<p>{_e(hook.doc)}</p>{_used_in(api, hook.name)}</section>"
    )


def _enum(api: Api) -> str:
    parts = []
    for enum in api.enums.values():
        rows = "".join(
            f"<tr><td class='name'><code>{_e(name)}</code></td><td>{value}</td></tr>" for name, value in enum.entries.items()
        )
        parts.append(
            f"<section id='{enum_anchor(enum.name)}'><h3><code>{_e(enum.name)}</code></h3><p>{_e(enum.doc)}</p>"
            f"<table><tr><th>Name</th><th>Value</th></tr>{rows}</table>{_used_in(api, enum.name)}</section>"
        )
    return "".join(parts)


def _member_signature(library: Library, member: Member) -> str:
    if member.kind == "function":
        separator = ":" if member.method else "."
        return f"{library.global_name}{separator}{member.name}({', '.join(member.params or [])})"
    return f"{library.global_name}.{member.name}"


def _library(api: Api, library: Library) -> str:
    rows = []
    for member in library.members:
        value = f"<code>{_e(member.value)}</code>" if member.value else ""
        rows.append(
            f"<tr><td class='name'><code>{_e(_member_signature(library, member))}</code></td>"
            f"<td>{_e(KIND_NAMES.get(member.kind, member.kind))}</td><td>{value}</td><td>{_e(member.doc)}</td></tr>"
        )
    return (
        f"<section id='{library_anchor(library)}'><h3><code>{_e(library.global_name)}</code></h3>"
        f"<p>Use it with <code>import \"{_e(library.import_name)}\"</code> (file <code>{_e(library.file)}</code>): "
        f"{_e(library.summary)}</p>"
        f"<table><tr><th>Member</th><th>Kind</th><th>Value</th><th></th></tr>{''.join(rows)}</table>"
        f"{_used_in(api, library.global_name)}</section>"
    )


def _limits(api: Api) -> str:
    rows = "".join(
        f"<tr><td class='name'><code>{_e(key)}</code></td><td>{_e(value)}</td></tr>" for key, value in api.limits.items()
    )
    sandbox = api.sandbox
    removed_fields = ", ".join(f"{lib}.{name}" for lib, names in sandbox.get("removedFields", {}).items() for name in names)
    lines = [
        ("Lua libraries", ", ".join(sandbox.get("libraries", []))),
        ("Removed globals", ", ".join(sandbox.get("removedGlobals", []))),
        ("Removed fields", removed_fields),
        ("Added globals", ", ".join(sandbox.get("runtimeGlobals", []))),
        ("Accepted without effect", ", ".join(f'import "{name}"' for name in api.noop_imports)),
    ]
    sandbox_rows = "".join(f"<tr><td class='name'>{_e(label)}</td><td>{_e(text)}</td></tr>" for label, text in lines if text)
    return (
        f"<h2 id='limits'>Limits and sandbox</h2><table><tr><th>Limit</th><th>Value</th></tr>{rows}</table>"
        f"<table><tr><th>Sandbox</th><th></th></tr>{sandbox_rows}</table>"
    )


def _nav(api: Api) -> str:
    def group(title: str, anchors: list[tuple[str, str]]) -> str:
        links = "".join(f"<a href='#{anchor}'>{_e(label)}</a>" for anchor, label in anchors)
        return f"<h2>{title}</h2>{links}" if anchors else ""

    return (
        "<nav>"
        + group("Commands", [(command_anchor(name), name) for name in api.commands])
        + group("Hooks", [(hook_anchor(name), name) for name in api.hooks])
        + group("Constants", [(enum_anchor(name), name) for name in api.enums])
        + group("Libraries", [(library_anchor(lib), lib.global_name) for lib in api.libraries])
        + group("Examples", [(example_anchor(e), e.title) for e in api.examples])
        + group("Complete scripts", [(script_anchor(s), f"{s.id}.lua") for s in api.scripts])
        + group("More", [("limits", "Limits and sandbox")])
        + "</nav>"
    )


def _focus_script(focus: str | None) -> str:
    if not focus:
        return ""
    return f"<script>var e = document.getElementById({json.dumps(focus)}); if (e) e.scrollIntoView();</script>"


def render_api_html(api: Api, focus: str | None = None) -> str:
    """focus is an anchor to scroll to when the page opens (a browser may drop the #fragment of a file URL)."""
    body = [
        f"<h1>{_e(api.title)} scripting API</h1>",
        f"<p class='muted'>Plugin <code>{_e(api.plugin)}</code>, Lua {_e(api.lua)}. Scripts are files "
        "<code>&lt;name&gt;.lua</code>; the plugin calls the hooks below and runs the commands the script calls.</p>",
        "<h2 id='commands'>Commands</h2>",
        "".join(_command(api, command) for command in api.commands.values()),
        "<h2 id='hooks'>Hooks</h2>",
        "".join(_hook(api, hook) for hook in api.hooks.values()),
    ]
    if api.enums:
        body += ["<h2 id='constants'>Constants</h2>", _enum(api)]
    if api.libraries:
        body += ["<h2 id='libraries'>Libraries</h2>", "".join(_library(api, library) for library in api.libraries)]
    body.append(_examples(api, Vocabulary(api)))
    body.append(_limits(api))
    return (
        "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        f"<title>{_e(api.title)} scripting API</title><style>{CSS}{_token_css()}{EXTRA_CSS}</style></head>"
        f"<body>{_nav(api)}<main>{''.join(body)}</main>{_focus_script(focus)}{COPY_SCRIPT if api.examples or api.scripts else ''}</body></html>"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="lua_editor.api.html", description=__doc__.splitlines()[0])
    parser.add_argument("plugin")
    parser.add_argument("--apis", type=Path, default=Path("apis"), help="folder with <plugin>/api.json files")
    parser.add_argument("--out", type=Path, help="file to write (default: standard output)")
    args = parser.parse_args(argv)
    api = ApiRegistry.from_folder(args.apis).for_path(args.plugin)
    if api is None:
        parser.error(f"no api.json for {args.plugin!r} in {args.apis}")
    page = render_api_html(api)
    if args.out:
        args.out.write_text(page, encoding="utf-8")
    else:
        sys.stdout.write(page)
    return 0


if __name__ == "__main__":
    sys.exit(main())
