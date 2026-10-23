"""Command line entry point."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PySide6.QtWidgets import QApplication

from lua_editor.api.model import ApiRegistry
from lua_editor.remote import DEVICE, REPO, LocalFS, Mount, MountFS, RemoteFS, RepoFS, ShellFS
from lua_editor.remote.shell import DEFAULT_HOST, DEFAULT_ROOT, SshRunner
from lua_editor.ui.mainwindow import MainWindow

DEFAULT_APIS = Path("apis")
# lua-editor/src/lua_editor/app.py: the repository is three folders up from the package
DEFAULT_REPO = Path(__file__).resolve().parents[3]


def build_filesystem(args: argparse.Namespace, parser: argparse.ArgumentParser) -> tuple[RemoteFS, str]:
    """The file system and the window title for the command line: repo + device mounts, or one root."""
    if args.root is not None:
        if not args.root.is_dir():
            parser.error(f"{args.root} is not a folder")
        return LocalFS(args.root), str(args.root.resolve())
    plugins = args.repo / "plugins"
    if not plugins.is_dir():
        parser.error(f"{args.repo} is not the repository (no plugins folder); --repo picks it")
    mounts = [Mount(REPO, lambda: RepoFS(plugins))]
    title = f"{REPO}: {args.repo.resolve()}"
    if not args.no_device:
        host = args.device or DEFAULT_HOST

        def open_device() -> RemoteFS:
            runner = SshRunner(host, args.user, key_file=args.key, accept_new_host=args.accept_new_host)
            return ShellFS(runner, args.device_root)

        mounts.append(Mount(DEVICE, open_device))
        title += f"   {DEVICE}: {host}:{args.device_root}"
    return MountFS(mounts), title


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="lua-editor", description="Editor for the Lua scripts of the jaypacks plugins.")
    parser.add_argument(
        "--repo", type=Path, default=DEFAULT_REPO, help="the repository; its plugins/<plugin>/scripts show as repo/"
    )
    parser.add_argument(
        "--device",
        nargs="?",
        const=DEFAULT_HOST,
        metavar="HOST",
        help=f"the Dwarf, shown as device/ and connected on first use (default host {DEFAULT_HOST}, scripts in {DEFAULT_ROOT})",
    )
    parser.add_argument("--no-device", action="store_true", help="edit the repo only, never connect")
    parser.add_argument("--root", type=Path, help="edit this one folder (laid out like the device's scripts folder) instead")
    parser.add_argument("--device-root", default=DEFAULT_ROOT, help="scripts folder on the device")
    parser.add_argument("--user", help="ssh user (default: from ~/.ssh/config, else root)")
    parser.add_argument("--key", help="ssh key file (default: from ~/.ssh/config, else the agent and default keys)")
    parser.add_argument("--accept-new-host", action="store_true", help="trust a host that is not in known_hosts yet")
    parser.add_argument("--apis", type=Path, default=DEFAULT_APIS, help="folder with <plugin>/api.json files")
    args = parser.parse_args(argv)
    if args.root is not None and (args.device or args.no_device):
        parser.error("--root edits one folder; it does not go with --device or --no-device")
    if args.device and args.no_device:
        parser.error("--device and --no-device exclude each other")
    fs, title = build_filesystem(args, parser)
    app = QApplication(sys.argv[:1])
    apis = ApiRegistry.from_folder(args.apis) if args.apis.is_dir() else ApiRegistry()
    window = MainWindow(fs, apis=apis)
    window.setWindowTitle(f"lua-editor  {title}")
    window.resize(1100, 700)
    window.show()
    status = app.exec()
    close = getattr(fs, "close", None)
    if close is not None:
        close()
    return status


if __name__ == "__main__":
    sys.exit(main())
