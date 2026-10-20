"""check-all: every passing features.json item's check, as fast as the machine allows.

  cache   A check whose inputs (its command, the binaries and the files it can read) are exactly as
          when it last passed is not run again: its log is restored. Only passes are cached; a
          failure always runs again. --no-cache runs everything (and caches its passes).
  order   Longest first (the last run's durations, build/check_cache/durations.json), on JOBS workers.
  groups  Checks that share a fixed port or fixed log paths never overlap (GROUPS); browser checks
          (two headless Chromes each) are capped at BROWSERS at a time.
  isolate Each game a check starts has its own user dir and miss file (RT_USER_DIR, RT_MISS_FILE),
          except run_native.sh's, whose logs/icall_miss.txt native.icall reads (one at a time).

Verdicts are the items' own check commands. logs/check_<id>.log per item, timings in
logs/check_all.log; prints REGRESSION <id> per failure and one summary line."""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / "build/check_cache"
WORK = ROOT / "build/check_all"
JOBS = int(os.environ.get("CHECK_JOBS", "8"))
BROWSERS = int(os.environ.get("CHECK_BROWSERS", "3"))
# (group, regex on the command): members run one at a time
GROUPS = [
    ("run_native", r"run_native\.sh"),
    ("net_test", r"net_test\.sh"),
    ("det_synctest", r"det_synctest\.sh"),
    ("fidelity", r"fidelity_report\.py"),
    ("difftest", r"difftest\.(sh|py)"),
]
BROWSER = r"python3\.14|net_web_test\.sh|web_check\.sh"
# Checks that judge timing (lockstep input delay, sync, frame pacing) fail on a loaded machine: one at a
# time, with at most BESIDE other checks next to it
TIMING = os.environ.get("CHECK_TIMING_IDS", r"^(ls|fps)\.|^net\.(web_lockstep|live_input|handoff)$")
BESIDE = int(os.environ.get("CHECK_BESIDE_TIMING", "2"))
NEVER_CACHE = os.environ.get("CHECK_NO_CACHE_IDS", r"^oracle\.wine$")
# Inputs by kind (paths under ROOT; directories are hashed by name, size and mtime of every file)
LIFT = ["gen/recomp", "analysis", "tools/lift", "tools/analysis", "runtime/include", "REVERSE.md"]
GAME = ["build/native/rugby08", "tools/check", "tests/replays"]
WEB = GAME + ["build/web", "build/rugby08-server", "server", "tools/net", "tools/web", "@chrome"]
STATIC = {
    r"^(analyze|lift\.(all|difftest|fidelity|reachability)|interp)\b": LIFT,
    r"^(setup|repo)\.|^pt\.(research|private_link)$|^ls\.ha_out$": ["@tree"],
    r"^web\.(compile|assets)$": ["build/web", "tools/check"],
    r"^oracle\.wine$": ["Rugby08_unpacked.exe", "tools/check"],
}

BINARIES = {"build/native/rugby08", "build/web/rugby08.wasm", "build/rugby08-server"}

_memo, _mu = {}, threading.Lock()


def file_hash(p):
    st = p.stat()
    h = hashlib.sha1()
    if str(p.relative_to(ROOT)) in BINARIES:  # their bytes: a relink may leave them as they were
        with open(p, "rb") as f:
            for b in iter(lambda: f.read(1 << 22), b""):
                h.update(b)
        return h.hexdigest()
    return f"{st.st_size}:{st.st_mtime_ns}"


def tree_stat(*paths):
    out = subprocess.run(
        ["git", "ls-files", "-co", "--exclude-standard", "--", *paths], cwd=ROOT, capture_output=True, text=True
    ).stdout.split()
    return [f"{f}:{file_hash(ROOT / f)}" for f in out if (ROOT / f).is_file()]


def input_key(name):
    with _mu:
        if name in _memo:
            return _memo[name]
    if name == "@tree":
        v = "\n".join(tree_stat())
    elif name == "@chrome":
        v = subprocess.run(
            ["defaults", "read", "/Applications/Google Chrome.app/Contents/Info", "CFBundleShortVersionString"],
            capture_output=True,
            text=True,
        ).stdout.strip()
    else:
        p = ROOT / name
        if p.is_dir():  # every file but Python's leftovers and this runner (scheduling is not a check input)
            files = (
                q
                for q in sorted(p.rglob("*"))
                if q.is_file() and "__pycache__" not in q.parts and q.name != "check_all.py"
            )
            v = "\n".join(f"{q.relative_to(ROOT)}:{file_hash(q)}" for q in files)
        else:
            v = file_hash(p) if p.exists() else "missing"
    v = hashlib.sha1(v.encode()).hexdigest()
    with _mu:
        _memo[name] = v
    return v


def inputs_of(cid, cmd):
    for rx, ins in STATIC.items():
        if re.search(rx, cid):
            return ins
    web = re.search(r"^(pt|ls|on|web)\.|^net\.(web_lockstep|live_input|handoff)$", cid)
    named = [  # the files the command names (its outputs under logs/ and build/ excepted)
        t
        for t in re.findall(r"[\w./-]+", cmd)
        if "/" in t and not t.startswith(("/", "logs/", "build/")) and (ROOT / t).exists()
    ]
    return (WEB if web else GAME) + named


def key_of(cid, cmd):
    h = hashlib.sha1(cmd.encode())
    for name in inputs_of(cid, cmd):
        h.update(f"{name}={input_key(name)}\n".encode())
    return h.hexdigest()


class Runner:
    def __init__(self, items, durations, use_cache):
        self.todo = sorted(items, key=lambda it: -durations.get(it[0], 60))
        self.locks = {g: threading.Lock() for g, _ in GROUPS}
        self.browsers = threading.Semaphore(BROWSERS)
        self.mu, self.fails, self.times, self.cached = threading.Lock(), [], {}, 0
        self.use_cache = use_cache
        self.running, self.timing = 0, False

    def take(self):
        """The longest waiting check whose group is free (its group lock taken), or None if all done."""
        while True:
            with self.mu:
                if not self.todo:
                    return None
                for i, (cid, cmd) in enumerate(self.todo):
                    tim = bool(re.search(TIMING, cid))
                    if tim and not self.timing and self.running > BESIDE:
                        break  # the next timing check goes first: let the others drain
                    if (self.timing or tim) and (tim and self.timing or self.running > BESIDE):
                        continue
                    grp = next((g for g, rx in GROUPS if re.search(rx, cmd)), None)
                    if grp and not self.locks[grp].acquire(blocking=False):
                        continue
                    self.running += 1
                    self.timing |= tim
                    return self.todo.pop(i), grp
            time.sleep(0.2)

    def run(self, cid, cmd):
        log, t0 = ROOT / f"logs/check_{cid}.log", time.time()
        key = None
        # use_cache: True reads and writes, False (--no-cache) only writes, None neither (the build)
        cacheable = self.use_cache is not None and not re.search(NEVER_CACHE, cid)
        if cacheable:
            key = key_of(cid, cmd)
            ent = CACHE / cid
            if self.use_cache and (ent / "key").exists() and (ent / "key").read_text() == key:
                shutil.copy(ent / "log", log)
                with self.mu:
                    self.cached += 1
                    self.times[cid] = (0, "cached")
                return
        env = dict(os.environ)
        if not re.search(r"run_native\.sh", cmd):
            d = WORK / cid
            (d / "userdata").mkdir(parents=True, exist_ok=True)
            env.update(RT_USER_DIR=str(d / "userdata"), RT_MISS_FILE=str(d / "icall_miss.txt"))
        if re.search(r"run_native\.sh", cmd):
            env["RUN_NATIVE_MEMO"] = str(WORK / "memo")
            env.setdefault("RUN_NATIVE_TIMEOUT", "600")  # a hang guard: the frames come slower beside other checks
        browser = re.search(BROWSER, cmd)
        if browser:
            self.browsers.acquire()
        try:
            with open(log, "w") as f:
                rc = subprocess.run(
                    ["bash", "-c", cmd], cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT
                ).returncode
        finally:
            if browser:
                self.browsers.release()
        dt = time.time() - t0
        with self.mu:
            self.times[cid] = (dt, f"rc={rc}")
            if rc:
                self.fails.append(cid)
                print(f"REGRESSION {cid}", flush=True)
        if cacheable and rc == 0:
            ent = CACHE / cid
            ent.mkdir(parents=True, exist_ok=True)
            shutil.copy(log, ent / "log")
            (ent / "key").write_text(key)  # the key of the inputs as they were when the run started

    def worker(self):
        while (job := self.take()) is not None:
            (cid, cmd), grp = job
            try:
                self.run(cid, cmd)
            except Exception as e:  # no verdict is a failure, never a silent pass
                with self.mu:
                    self.times[cid] = (0, f"error {e!r}")
                    self.fails.append(cid)
                print(f"REGRESSION {cid} (check_all: {e!r})", flush=True)
            finally:
                if grp:
                    self.locks[grp].release()
                with self.mu:
                    self.running -= 1
                    if re.search(TIMING, cid):
                        self.timing = False


def main():
    os.chdir(ROOT)
    use_cache = "--no-cache" not in sys.argv
    only = os.environ.get("CHECK_ONLY", "")  # a regex on ids: a subset (tests of this script)
    items = [
        (i["id"], i["check"])
        for i in json.load(open("features.json"))["items"]
        if i.get("passes") and re.search(only, i["id"])
    ]
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    CACHE.mkdir(parents=True, exist_ok=True)
    dur_file = CACHE / "durations.json"
    durations = json.loads(dur_file.read_text()) if dur_file.exists() else {}
    t0 = time.time()
    # The binary first: most checks run it (and their cache keys hash it).
    first = [it for it in items if it[0] == "native.compile"]
    r = Runner(first, durations, None)
    r.worker()
    r.use_cache = use_cache
    r.todo = sorted([it for it in items if it[0] != "native.compile"], key=lambda it: -durations.get(it[0], 60))
    ts = [threading.Thread(target=r.worker) for _ in range(JOBS)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    for cid, _ in items:  # every item has a verdict
        if cid not in r.times:
            r.fails.append(cid)
            print(f"REGRESSION {cid} (check_all: not run)")
    total = time.time() - t0
    with open("logs/check_all.log", "w") as f:
        for cid, (dt, what) in sorted(r.times.items(), key=lambda kv: -kv[1][0]):
            f.write(f"{cid} {what} {dt:.0f}s\n")
        f.write(f"total {total:.0f}s, {r.cached} cached, jobs {JOBS}\n")
    durations.update({cid: dt for cid, (dt, what) in r.times.items() if what != "cached"})
    dur_file.write_text(json.dumps(durations, indent=0))
    n = len(items)
    if r.fails:
        print(f"check-all: {len(r.fails)} of {n} regressed ({total:.0f}s, {r.cached} cached, logs/check_all.log)")
        sys.exit(1)
    print(f"check-all: no regressions ({n} items, {total:.0f}s, {r.cached} cached)")


if __name__ == "__main__":
    main()
