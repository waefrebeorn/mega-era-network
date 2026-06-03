# Repo Plan & Data Organization

Goal: shrink the monorepo while steering all bounded, artifact, and operational concerns into clear storage zones. No production wiring is finalized here yet.

## Problem
- `/home/wubu2/money-room` tracks 14.7 GB.
- `.git` is already ~695 MB, with known tracked large-file history and runtime leftovers from incomplete moves.
- `.gitignore` still lets large files upset a clean push/workflow unless the layout is explicit.

## Approach

### Git history / tracked files
- Remove bulk runtime tracked files from history via `git-filter-repo`.
- Replace these paths with external storage outside the git worktree:
  - `data/backups/*`
  - `engine/timeline.db*`
  - any other multi-megabyte generated artifacts

### Runtime storage
- Move runtime and state files to `/mnt/c/money-room-runtime/src/...` with symlinks back to the source tree where path compatibility is required.
- Actual DB files live outside the repo (e.g., `/mnt/c/money-room-data/`).
- Mount-point hierarchy:
  - `/mnt/c/money-room-runtime/src/<project>/.run/` — current runtime
  - `/mnt/c/money-room-runtime/src/<project>/.bak/` — backups, old snapshots
  - `/mnt/c/money-room-runtime/src/<project>/export/` — exports, bundles

### Generated / build outputs
- Engineering-borne build output lives under `build/`, `bin/`, `lib/`, `docs/build/`, all gitignored.
- CI build artifacts and coverage go to `/mnt/c/money-room-runtime/ci/build-<timestamp>/*`.

### Data zones inside repo
- `data/` — schemas, fixtures, small config JSON that is legitimately versioned.
- `docs/data/` — report outputs and derived dashboards.
- No huge raw DB or backup under either tree unless it is artifact-managed and intentional.

## Sandbox auth schema (planned)
- File: `docs/data/registrations.json`
- Record layout:
  - Key, name/email, tier, expires_in (seconds)
  - created_at, revoked_at
- Dev sandbox (expired/revoked qualifiers when active): `waefrebeorn` / `513513a`
- Purpose: tracked 1-second signup advertising data export API.
- Never embed credentials in source; consume from env or registration store.go