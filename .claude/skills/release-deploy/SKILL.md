---
name: release-deploy
description: Cut a keyboardTD release and reload the game on the remote server (app-server-2 / td.zboina.pl). Use when asked to release, deploy, push a new version to the server, or reload the remote container.
---

## ⚠️ ONE-TIME: finish the `0.4.1` release — DELETE THIS SECTION WHEN DONE

Commit `c128111` ("fix: keep boss words from clashing with an active
power-up's letter") and tag `0.4.1` are staged locally on `main` in the
sandbox but **not pushed** — `git push origin main --tags` failed from the
sandbox with `Connection closed by 140.82.121.4 port 22` (no GitHub SSH
credentials here; `origin` is `git@github.com:zboinek/keyboardTD.git`).

Server-side prep: checked, nothing to change. `~/apps/docker-compose.yml`
on app-server-2 already has both the `./keyboardtd-data:/data` volume and
the `5555:5555` port mapping for the `keyboardtd` service. No backup file
was made since nothing was edited.

Remaining steps, run from your **host terminal** (not the sandbox):

```sh
git push origin main --tags
```

Then continue with steps 2-4 of this skill (below) as normal: poll the
`docker` GitHub Actions run for tag `0.4.1` until it's `completed success`,
then `ssh app-server-2 'cd ~/apps && docker compose pull keyboardtd && docker compose up -d keyboardtd'`,
then verify per step 4.

When done, delete this whole section (there's no `.bak-0.4.1` file to clean
up).

# Releasing keyboardTD and reloading app-server-2

Releases are tag-driven; the server only ever runs `ghcr.io/zboinek/keyboardtd:latest`.
Deploying = push a tag, let CI build the image, then pull + restart on the server.

## 0. Sandboxed agent? Check push access first

This skill sometimes runs inside a sandboxed agent that has a forwarded SSH
agent (so `ssh app-server-2` works) but no GitHub credentials (so `git push`
to GitHub hangs or fails auth). Server-side SSH work and the GitHub push are
independent — losing one doesn't mean losing the other.

If `git push origin main --tags` (step 1) fails or hangs from the sandbox:

1. Do NOT work around it — no alternate remotes, no committing credentials,
   no bypassing hooks. The push has to happen from a human's host terminal.
2. Still do whatever server-side prep you can from the sandbox over SSH —
   e.g. compose file edits described in step 3. There's no `~/.ssh/config`
   in the sandbox, so address the server explicitly:
   `ssh -p 3000 zboinek@195.167.155.15 ...` (the forwarded agent
   authenticates). Back up `~/apps/docker-compose.yml` before editing it
   (`cp docker-compose.yml docker-compose.yml.bak-<version>`).
3. Add a "⚠️ ONE-TIME: finish the `<version>` release — DELETE THIS SECTION
   WHEN DONE" section at the very top of this file (above this one), stating:
   the commit/tag staged locally but unpushed, exactly what server-side prep
   is already done (and where the backup file is, if any), and the precise
   remaining commands — starting with `git push origin main --tags` — to run
   **from the host terminal, not the sandbox**, through steps 2-4 below,
   ending with deleting that section and any `.bak-<version>` file.
4. Tell the user directly that the release can't be finished from the
   sandbox and ask them to run those steps from their host terminal.

## 1. Tag and push

Tags are bare versions (`0.1.0`, `0.1.1`, ...). Pick the next semver
(`git tag -l` to see existing), then from a clean, pushed `main`:

```sh
git tag <version>
git push origin main --tags
```

This triggers two GitHub Actions workflows:
- **docker** — builds the WebAssembly image and pushes
  `ghcr.io/zboinek/keyboardtd:<version>` and `:latest` (amd64 + arm64).
- **release** — attaches Linux/macOS/Windows terminal binaries to a GitHub
  Release.

## 2. Wait for the docker workflow

The server pulls `:latest`, so reloading before the **docker** workflow
finishes redeploys the *old* image. The `gh` CLI is not installed on this
machine; poll the public API instead:

```sh
curl -s "https://api.github.com/repos/zboinek/keyboardTD/actions/runs?per_page=4" \
  | python3 -c "import json,sys; [print(r['name'], r['status'], r.get('conclusion'), r['head_branch']) for r in json.load(sys.stdin)['workflow_runs']]"
```

Wait until the `docker` run for the new tag shows `completed success`
(takes several minutes — it cross-compiles for arm64 under QEMU).

## 3. Reload the server

`app-server-2` is defined in `~/.ssh/config` (zboinek@195.167.155.15:3000).
The game is the `keyboardtd` service in `~/apps/docker-compose.yml`, served
at https://td.zboina.pl behind an nginx-proxy + letsencrypt companion
(don't touch those containers).

Since 0.3.x the image also runs the hall-of-fame API (nginx `/api/` -> a
Python sidecar; SQLite at `/data/scores.db`). The compose service MUST
mount a volume for `/data` or scores are lost on every redeploy:

```yaml
    volumes:
      - ./keyboardtd-data:/data
```

Check it's there before reloading; add it once if missing. The API's rate
limiting reads `X-Real-IP`, which the outer nginx-proxy sets by default.

Since the SSH frontend the image also runs `ktd-ssh` on container port
5555 (`ssh td.zboina.pl` works because port 22 on the public IP is NATed
to port 5555 on app-server-2's internal address). The compose service
MUST publish it:

```yaml
    ports:
      - "5555:5555"
```

The `/data` volume now also holds the SSH host key
(`ssh_host_ed25519_key`) — losing it means every returning player gets a
scary host-key-changed warning, one more reason the volume must stay.

```sh
ssh app-server-2 'cd ~/apps && docker compose pull keyboardtd && docker compose up -d keyboardtd'
```

## 4. Verify

```sh
ssh app-server-2 'docker ps --filter name=keyboardtd --format "{{.Image}} {{.Status}}"'
curl -sI https://td.zboina.pl | head -1   # expect HTTP 200
ssh -o StrictHostKeyChecking=no td.zboina.pl exit   # expect the "no commands here" notice
```

Confirm the running image digest changed / the container restarted recently.
If the site serves stale content, it's browser/xterm.js caching — the nginx
proxy itself doesn't cache.
