"""The Dwarf's scripts folder over ssh.

The Dwarf's sshd has no SFTP, so every operation runs a small command in the device's busybox shell (`stat -c`, `find`,
`cat`, `mv`, `mkdir`, `rm`) through paramiko's exec_command. A command runner is injected, so the file system logic is
separate from the connection.
"""

from __future__ import annotations

import posixpath
import shlex
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

import paramiko

from lua_editor.remote.base import TEMP_SUFFIX, Entry, NotFound, RemoteError, RemoteFS

DEFAULT_HOST = "192.168.51.1"
DEFAULT_ROOT = "/data/jaypacks/scripts"
STAT_FORMAT = "%F|%s|%Y|%n"
_MISSING = ("No such file or directory", "Not a directory")


@dataclass(frozen=True)
class Result:
    status: int
    out: bytes
    err: str


class Runner(Protocol):
    def run(self, command: str, stdin: bytes | None = None) -> Result: ...

    def close(self) -> None: ...


class SshRunner:
    """One ssh connection, one command per call. Host keys come from known_hosts, the user and key from ~/.ssh/config."""

    def __init__(
        self,
        host: str = DEFAULT_HOST,
        user: str | None = None,
        port: int | None = None,
        key_file: str | None = None,
        accept_new_host: bool = False,
        timeout: float = 10.0,
    ) -> None:
        config = _ssh_config(host)
        self._timeout = timeout
        self._client = paramiko.SSHClient()
        self._client.load_system_host_keys()
        self._client.set_missing_host_key_policy(paramiko.AutoAddPolicy if accept_new_host else paramiko.RejectPolicy)
        identity = key_file or (config.get("identityfile") or [None])[0]
        try:
            self._client.connect(
                config.get("hostname", host),
                port=port or int(config.get("port", 22)),
                username=user or config.get("user", "root"),
                key_filename=str(Path(identity).expanduser()) if identity else None,
                timeout=timeout,
                banner_timeout=timeout,
                auth_timeout=timeout,
            )
        except paramiko.BadHostKeyException as error:
            raise RemoteError(f"host key of {host} differs from known_hosts: {error}") from error
        except (paramiko.SSHException, OSError) as error:
            raise RemoteError(f"cannot connect to {host}: {error}") from error

    def run(self, command: str, stdin: bytes | None = None) -> Result:
        try:
            channel_in, channel_out, channel_err = self._client.exec_command(command, timeout=self._timeout)
            if stdin is not None:
                channel_in.write(stdin)
                channel_in.flush()
            channel_in.channel.shutdown_write()
            out = channel_out.read()
            err = channel_err.read().decode("utf-8", "replace").strip()
            return Result(channel_out.channel.recv_exit_status(), out, err)
        except (paramiko.SSHException, OSError, EOFError) as error:
            raise RemoteError(f"ssh command failed: {error}") from error

    def close(self) -> None:
        self._client.close()


def _ssh_config(host: str) -> dict:
    path = Path("~/.ssh/config").expanduser()
    if not path.is_file():
        return {}
    with open(path, encoding="utf-8") as handle:
        return paramiko.SSHConfig.from_file(handle).lookup(host)


class ShellFS(RemoteFS):
    def __init__(self, runner: Runner, root: str = DEFAULT_ROOT) -> None:
        self._runner = runner
        self._root = root.rstrip("/")

    def close(self) -> None:
        self._runner.close()

    def _full(self, path: str) -> str:
        return shlex.quote(posixpath.join(self._root, path) if path else self._root)

    def _check(self, path: str, result: Result) -> Result:
        if result.status == 0:
            return result
        if any(text in result.err for text in _MISSING):
            raise NotFound(path)
        raise RemoteError(f"{path!r}: {result.err or 'status ' + str(result.status)}")

    @staticmethod
    def _entry(path: str, line: str) -> Entry:
        kind, size, mtime, _ = line.split("|", 3)
        is_dir = kind.startswith("directory")
        return Entry(posixpath.basename(path), path, is_dir, 0 if is_dir else int(size), int(mtime))

    def _stat(self, path: str) -> Entry:
        result = self._check(path, self._runner.run(f"stat -L -c '{STAT_FORMAT}' {self._full(path)}"))
        return self._entry(path, result.out.decode("utf-8", "replace").strip())

    def _list(self, path: str) -> list[Entry]:
        command = f"find -L {self._full(path)} -mindepth 1 -maxdepth 1 -exec stat -L -c '{STAT_FORMAT}' {{}} +"
        result = self._check(path, self._runner.run(command))
        entries = []
        for line in result.out.decode("utf-8", "replace").splitlines():
            name = posixpath.basename(line.split("|", 3)[3])
            if not name.endswith(TEMP_SUFFIX):
                entries.append(self._entry(posixpath.join(path, name), line))
        return entries

    def _read(self, path: str) -> bytes:
        return self._check(path, self._runner.run(f"cat {self._full(path)}")).out

    def _write(self, path: str, data: bytes) -> None:
        target = self._full(path)
        temp = self._full(posixpath.join(posixpath.dirname(path), "." + posixpath.basename(path) + TEMP_SUFFIX))
        self._check(path, self._runner.run(f"cat > {temp} && mv -f {temp} {target} || {{ rm -f {temp}; exit 1; }}", data))

    def _mkdir(self, path: str) -> None:
        self._check(path, self._runner.run(f"mkdir {self._full(path)}"))

    def _rename(self, source: str, target: str) -> None:
        self._check(source, self._runner.run(f"mv {self._full(source)} {self._full(target)}"))

    def _delete(self, path: str) -> None:
        full = self._full(path)
        self._check(path, self._runner.run(f"if [ -d {full} ]; then rmdir {full}; else rm {full}; fi"))
