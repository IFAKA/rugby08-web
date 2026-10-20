# tools/check/ — checks (features.json `check` commands) and gates

`run_native.sh` / `web_check.sh` / `serve_check.sh` feature checks · `run_wine.sh` the original under Wine
· `det_synctest.sh` deterministic-mode synctest · `regress.sh` + `regress_cmp.py` refactor referee (deterministic
captures must match pixel for pixel and hash for hash) · `lint.sh` formatters/linters · `structure.sh` size and
boundary rules · `img_stats.py`, `audio_stats.py`, `check_asset_manifest.py`, `check_tools.sh`.
`check_all.py` (`./init.sh check-all`) runs the passing items on `CHECK_JOBS` workers (default 8), longest
first. A check whose inputs (command, binaries by content, the files it can read) are as when it last passed is
skipped and its log restored (`build/check_cache/`; `--no-cache` runs all; failures always rerun). Checks sharing
a port or fixed log path never overlap (`GROUPS`), at most `CHECK_BROWSERS` browser checks run at once, and every
game gets its own `RT_USER_DIR` and `RT_MISS_FILE`. Timings per item: `logs/check_all.log`.
