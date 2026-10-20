"""Print a free localhost TCP port, so checks (and worktrees) running side by side never share one."""

import socket

with socket.socket() as s:
    s.bind(("127.0.0.1", 0))
    print(s.getsockname()[1])
