---
name: release-deploy
description: Cut a keyboardTD release and reload the game on the remote server (app-server-2 / td.zboina.pl). Use when asked to release, deploy, push a new version to the server, or reload the remote container.
---

# Releasing keyboardTD and reloading app-server-2

## ⚠️ ONE-TIME: finish the 0.4.0 (SSH frontend) release — DELETE THIS SECTION WHEN DONE

State as of 2026-07-12: commit `bf5109b` + tag `0.4.0` (feat: play over ssh)
exist on local `main` but are NOT pushed — the sandbox couldn't authenticate
to GitHub, so the push happens from the host terminal. The server-side
compose edit is ALREADY DONE (`ports: "5555:5555"` added to the keyboardtd
service on app-server-2; backup at `~/apps/docker-compose.yml.bak-0.4.0`;
the `/data` volume was already present). Remaining steps:

1. From the host terminal (not the sandbox): `git push origin main --tags`
2. Wait for the **docker** workflow for `0.4.0` to complete (step 2 below).
3. Reload: `ssh app-server-2 'cd ~/apps && docker compose pull keyboardtd && docker compose up -d keyboardtd'`
   (from the sandbox use: `ssh -p 3000 zboinek@195.167.155.15 ...` — no
   ~/.ssh/config in there, the forwarded agent authenticates)
4. Verify per step 4 below, including `ssh td.zboina.pl` showing the game.
5. Delete this whole section and the `.bak-0.4.0` file on the server.



Releases are tag-driven; the server only ever runs `ghcr.io/zboinek/keyboardtd:latest`.
Deploying = push a tag, let CI build the image, then pull + restart on the server.

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
