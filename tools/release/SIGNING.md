# Release Manifest Signing

Releases are signed so the updater can tell a real AwowLab release from a
tampered one. The signature covers `manifest.json`; every file the updater
downloads is in turn hash-checked against that manifest, so one signature
protects the whole release.

## One-time setup

1. Create the key pair (you will be asked for a password):

   ```
   python tools/release/generate-signing-key.py
   ```

   By default the private key lands in `%USERPROFILE%\awowlab-signing\`.
   **Back it up now** (password manager attachment or encrypted drive).
   It must never be committed to either repository.

2. The script prints the public key as 128 hex characters and offers to
   write it into `signing_public_key` in `awowlab.config.json` at the
   repository root (`--write-config` does it without asking). That file is
   your own and gitignored; keep a copy with your key backup, re-run CMake
   and rebuild the updater. The private key never goes in that file.

Once a key is configured, signatures are required: the updater refuses a
release whose `manifest.json.sig` is missing, can't be downloaded or doesn't
match. Every release from then on has to be signed.

## Running your own update feed

A fork that publishes its own releases (see `AWOWLAB_UPDATE_REPO` in
`docs/BUILD-OPTIONS.md`) can't sign with the official key, so its updater
would refuse every release. Generate your own key pair with
`tools/release/generate-signing-key.py`, put your public key in
`signing_public_key` in `awowlab.config.json` (the script offers to), and
sign your releases with your private key as described below.

## Per-release

`generate-manifest.py` signs automatically when it can find the key:

```
set AWOWLAB_SIGNING_KEY=C:\Users\you\awowlab-signing\awowlab-signing-key.pem
python tools/release/generate-manifest.py release 0.18.0
```

or pass `--sign-key <path>` explicitly. Upload `manifest.json.sig` and
`SHA256SUMS.txt` along with the usual release assets.

`tools/release/release.py` does all of this as part of a release. It stops
before building when the key is not set, and checks the new signature against
`signing_public_key` in `awowlab.config.json`, so a release signed with a key
the updater does not trust never gets as far as `--publish`.

## If the key is lost

Auto-update keeps working for releases already published. To sign new
releases, generate a new key pair, put the new public key in
`awowlab.config.json`, and publish a build made with it — users on old
builds have to download that build manually once. Treat this as a last resort; back the key up properly instead.

## If the key is compromised

Rotate immediately: new key pair, the new public key in
`signing_public_key` in `awowlab.config.json`, a new build made with it, and
delete any releases signed with the old key. Because the key only signs
manifests (it is not a code-signing certificate), revoking it is entirely
under your control — there is no CA involved.
