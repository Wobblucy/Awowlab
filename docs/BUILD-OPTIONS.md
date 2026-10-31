# Build options

CMake cache variables that change what a build of AwowLab does. Set them on
the configure line, for example:

```
cmake --preset x64-release -DAWOWLAB_ENABLE_UPDATER=OFF
```

They end up in a generated header, `AwowLabBuildOptions.h`, in the build
folder. Changing one rebuilds only the files that use it.

## awowlab.config.json

Where a build points lives in one file at the repository root,
`awowlab.config.json`. Every builder has their own copy and it is never
committed (it is gitignored). The first configure creates it from
`awowlab.config.example.json` and fills in `settings_key` (see below). A fork
edits this file and nothing else: its own project page, release repository,
assets repository, WarcraftLogs client, signing key and DeepL key. When the
example gains a field, the next configure copies it into your file with the
example's value, so your file always lists everything there is to set.

| Field | Cache variable | |
|-------|----------------|-|
| `project_url` | `AWOWLAB_PROJECT_URL` | Project web page; the landing page links its `/issues` and `/releases` |
| `update_repo` | `AWOWLAB_UPDATE_REPO` | Repository (`owner/name`) the updater installs releases from |
| `assets_repo` | `AWOWLAB_ASSETS_REPO` | Repository (`owner/name`) holding the static assets release |
| `wcl_client_id` | `AWOWLAB_WCL_CLIENT_ID` | WarcraftLogs OAuth client id |
| `wcl_redirect_ports` | `AWOWLAB_WCL_REDIRECT_PORTS` | Local ports sign-in listens on, tried in order; see [WarcraftLogs client](#warcraftlogs-client-id) |
| `signing_public_key` | `AWOWLAB_SIGNING_PUBLIC_KEY` | Public half of the release signing key, 128 hex characters |
| `signing_key_path` | | Private signing key file the release tools sign with; `AWOWLAB_SIGNING_KEY` overrides it |
| `settings_key` | `AWOWLAB_SETTINGS_KEY` | What the settings file key is derived from; see [Settings key](#settings-key) |
| `settings_key_generated` | | `true` when CMake made `settings_key` up; releases refuse such a key |
| `deepl_api_key` | | DeepL key for `tools/translate/translate_all.py` |

The example holds the official repositories and client id. CMake reads the file at configure
time and re-runs on its own when the file changes. Each field is the default
of its cache variable, so a `-D` on the configure line still wins:

```
cmake --preset x64-debug -DAWOWLAB_UPDATE_REPO=someone/test-feed
```

A value set with `-D` stays in that build folder's cache until it is set
again (or removed with `-U`); values nobody overrode follow the file. To use
a different file altogether, pass `-DAWOWLAB_CONFIG_FILE=<path>`.

The release tools read the same file through `tools/release/awowlab_config.py`:
`release.py` and `generate-manifest.py` take the release and assets
repositories from it, and `tools/fetch-assets.py` downloads from `assets_repo`.

The private signing key itself never goes in this file. It stays outside the
repository; `signing_key_path` only names where it is. The
`AWOWLAB_SIGNING_KEY` environment variable or `--sign-key` override that path.

## Updater

| Variable | Default | |
|----------|---------|-|
| `AWOWLAB_ENABLE_UPDATER` | `OFF` (`ON` in the `x64-release` preset) | Build `AwowLabUpdater.exe` and let the app update itself |
| `AWOWLAB_UPDATE_REPO` | `update_repo` in `awowlab.config.json` | GitHub repository (`owner/name`) whose latest release the updater installs |
| `AWOWLAB_SIGNING_PUBLIC_KEY` | `signing_public_key` in `awowlab.config.json` | Key the updater checks release signatures against |

The updater installs whatever the latest release in `AWOWLAB_UPDATE_REPO`
is, replacing `AwowLab.exe` and the data next to it. On a build of your own
that means your build is swapped for the official one the first time it
updates, and any changes you made are gone. So the updater is off unless a
build asks for it.

Which presets turn it on:

| Preset | Updater |
|--------|---------|
| `x64-release` | on (official releases are built from this preset) |
| `x64-relwithdebinfo` | off |
| `x64-debug`, `x86-*` | off |
| no preset | off |

If you build Release from source with the `x64-release` preset, add
`-DAWOWLAB_ENABLE_UPDATER=OFF` unless you want the official updates.

With the updater off:

- `AwowLabUpdater.exe` is not built and not copied next to the app.
- The landing page shows "Automatic updates are turned off in this build"
  where the Check for Updates button would be.
- The language pack prompt (shown for Korean and Chinese) still appears but has no download button, since packs are installed by the
  updater. If you need one, copy `data/spellnames/` from an official install
  that has the pack.
- A staged updater in `updater/pending/` is not installed on startup.

### Publishing your own updates

Set `update_repo` (and `assets_repo`, if your static assets live elsewhere)
in `awowlab.config.json` to your repositories and turn the updater on. Each
release needs the same assets as an official one (`manifest.json`,
`version.json` and the files the manifest lists; see `tools/release/generate-manifest.py`).

Signatures are required. The updater checks each release's
`manifest.json.sig` against `signing_public_key`, which is the official key
until you change it, and refuses a release that has no `.sig` or one that
doesn't match. To run your own feed, generate your own key pair with
`tools/release/generate-signing-key.py` (it offers to write your public key
into `awowlab.config.json`) and sign every release you publish. See
`tools/release/SIGNING.md`. Configuring with the updater on and an empty
`signing_public_key` fails, since such an updater would install anything.

## WarcraftLogs client id

| Variable | Default | |
|----------|---------|-|
| `AWOWLAB_WCL_CLIENT_ID` | `wcl_client_id` in `awowlab.config.json` (the official AwowLab client) | OAuth client id used for WarcraftLogs sign-in |

Sign-in is the OAuth authorization code flow with PKCE. The app opens the
browser at `https://www.warcraftlogs.com/oauth/authorize`, waits for the
redirect on a local port, then exchanges the code at `/oauth/token` with the
client id and the PKCE verifier. No client secret is sent.

To use your own client:

1. On warcraftlogs.com, create an API client and mark it as a public client,
   since the token request carries no secret.
2. Add a redirect URL for every port in `wcl_redirect_ports`. The app binds
   the first free port of these and sends the matching URL, so any of them can
   be the one in use. With the default ports:

   ```
   http://localhost:8080/callback
   http://localhost:8765/callback
   http://localhost:8766/callback
   http://localhost:8767/callback
   ```

   The host must be `localhost` and the path `/callback`, exactly as written.
3. Put your client id in `wcl_client_id` in `awowlab.config.json`, or, for
   one build folder only, configure with it:

   ```
   cmake --preset x64-debug -DAWOWLAB_WCL_CLIENT_ID=<your client id>
   ```

To use other ports, list them in `wcl_redirect_ports` and register the same
set on the client. The URL format is in
`AwowLab/warcraftlogs_manager/include/Auth/Credentials.h`.

## Settings key

| Variable | Default | |
|----------|---------|-|
| `AWOWLAB_SETTINGS_KEY` | `settings_key` in `awowlab.config.json` | Key material for the settings file |

The app keeps its settings in `User Settings/settings.dat`, encrypted with
AES-256 under SHA-256 of `settings_key` (`AwowLab/Core/Settings/Crypto.cpp`).
The value is compiled into the program, so this keeps the file from being
read casually rather than keeping it secret.

When `settings_key` is empty or missing, configure fills it with a random
value, sets `settings_key_generated` to `true` and says so. The value stays in
the file, so later builds from the same checkout read the same settings. A
build with a different key cannot read the file and starts from defaults.

That is fine for your own builds, but a release has to keep the key its
users' settings were written with. `tools/release/release.py` refuses to build
a release when the file is missing, `settings_key` is empty, or
`settings_key_generated` is `true`. To release, put the release key in
`settings_key`, set `settings_key_generated` to `false` (or remove it), and
configure again. `python tools/check_setup.py` warns while the key is a
generated one.
