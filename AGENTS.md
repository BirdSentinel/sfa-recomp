# Source-only repository

Keep game assets, generated guest code, SDK binaries, temporary captures, saves,
and build outputs out of version control. `.gitignore` documents these exclusions.
Recreate local game changes with `snupi/tools/prepare_game.py` and obtain the
patched SDK source with `tools/prepare_sdk.ps1`. See `README.txt` for build steps.

When this folder is the `git` export inside the original development workspace,
edit the original working source outside this folder, then run that workspace's
`tools/sync_repo.py` and `tools/sync_repo.py --check`. Do not edit only this copy.
In a standalone clone, edit this repository directly; no nested export is needed.
