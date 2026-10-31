"""Read awowlab.config.json, the file that says where this build points.

The release tools and CMake read the same file, so a builder changes their
repositories and keys in one place. The file is each builder's own and is not
committed: CMake creates it from awowlab.config.example.json on the first
configure. Until then load() reads the example, so tools that only need the
public repositories work in a fresh checkout. Standard library only.

    import awowlab_config
    config = awowlab_config.load()
    config.update_repo        # "owner/name"
    config.github_url(config.assets_repo, "releases/download/assets-v5")

A field missing from the builder's file takes the example's value, the same
way CMake fills it in on the next configure.

Scripts outside tools/release add that folder to sys.path first; see
tools/fetch-assets.py.
"""
import json
import os
import re
from dataclasses import dataclass

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
CONFIG_PATH = os.path.join(REPO_ROOT, "awowlab.config.json")
EXAMPLE_PATH = os.path.join(REPO_ROOT, "awowlab.config.example.json")

_REPO_PATTERN = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
_CLIENT_ID_PATTERN = re.compile(r"^[A-Za-z0-9-]+$")
_KEY_PATTERN = re.compile(r"^[0-9A-Fa-f]{128}$")
_URL_PATTERN = re.compile(r"^https://[A-Za-z0-9._~/%-]+[^/]$")


class ConfigError(Exception):
    pass


@dataclass(frozen=True)
class Config:
    path: str
    project_url: str
    update_repo: str
    assets_repo: str
    wcl_client_id: str
    signing_public_key: str
    # Key material for the settings file. Empty when the build would make
    # one up; settings_key_generated is true when CMake made it up.
    settings_key: str = ""
    settings_key_generated: bool = False
    # Private release signing key file; AWOWLAB_SIGNING_KEY overrides it
    signing_key_path: str = ""
    # DeepL key for tools/translate/translate_all.py
    deepl_api_key: str = ""

    @property
    def signing_key(self):
        """Path of the private signing key, or "" when none is set."""
        return os.environ.get("AWOWLAB_SIGNING_KEY", "") or self.signing_key_path

    @property
    def is_example(self):
        """True when this is the committed template, not the builder's own file."""
        return os.path.normcase(os.path.abspath(self.path)) == os.path.normcase(EXAMPLE_PATH)

    @staticmethod
    def github_url(repo, rest=""):
        url = f"https://github.com/{repo}"
        return f"{url}/{rest}" if rest else url

    @staticmethod
    def split_repo(repo):
        owner, name = repo.split("/")
        return owner, name


def check_repo(value, field="repository"):
    if not _REPO_PATTERN.match(value or ""):
        raise ConfigError(f"{field} must look like owner/name, got {value!r}")
    return value


def check_public_key(value):
    """Return the key in lower case, or raise. Empty means no key."""
    if value == "":
        return value
    if not _KEY_PATTERN.match(value):
        raise ConfigError("signing_public_key must be 128 hex characters (64 bytes), "
                          f"got {len(value)} characters")
    return value.lower()


def _read(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as error:
        raise ConfigError(f"cannot read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise ConfigError(f"{path} is not valid JSON: {error}") from error


def load(path=CONFIG_PATH):
    if path == CONFIG_PATH and not os.path.isfile(path) and os.path.isfile(EXAMPLE_PATH):
        path = EXAMPLE_PATH
    data = _read(path)
    if os.path.isfile(EXAMPLE_PATH) and os.path.abspath(path) != os.path.abspath(EXAMPLE_PATH):
        example = _read(EXAMPLE_PATH)
        data = {**{k: v for k, v in example.items() if k != "settings_key"}, **data}

    def field(name):
        value = data.get(name)
        if not isinstance(value, str):
            raise ConfigError(f"{path}: \"{name}\" is missing or not a string")
        return value

    project_url = field("project_url")
    if not _URL_PATTERN.match(project_url):
        raise ConfigError(f"{path}: project_url must be an https:// address with no "
                          f"trailing slash, got {project_url!r}")
    client_id = field("wcl_client_id")
    if not _CLIENT_ID_PATTERN.match(client_id):
        raise ConfigError(f"{path}: wcl_client_id is not a WarcraftLogs client id: {client_id!r}")
    # A config written before settings_key existed has none; CMake fills it in.
    settings_key = data.get("settings_key", "")
    if not isinstance(settings_key, str):
        raise ConfigError(f"{path}: \"settings_key\" is not a string")
    generated = data.get("settings_key_generated", False)
    if not isinstance(generated, bool):
        raise ConfigError(f"{path}: \"settings_key_generated\" is not true or false")

    def optional(name):
        value = data.get(name, "")
        if not isinstance(value, str):
            raise ConfigError(f"{path}: \"{name}\" is not a string")
        return value
    try:
        return Config(
            path=path,
            project_url=project_url,
            update_repo=check_repo(field("update_repo"), "update_repo"),
            assets_repo=check_repo(field("assets_repo"), "assets_repo"),
            wcl_client_id=client_id,
            signing_public_key=check_public_key(field("signing_public_key")),
            settings_key=settings_key,
            settings_key_generated=generated,
            signing_key_path=optional("signing_key_path"),
            deepl_api_key=optional("deepl_api_key"),
        )
    except ConfigError as error:
        raise ConfigError(f"{path}: {error}") from error


def release_settings_key_problem(config):
    """Why this config cannot build a release, or None if it can.

    The settings key decides whether the new build can read the settings
    file every user already has. A release built with a missing or made-up
    key resets all of them to defaults.
    """
    how = ("Put the release settings key in \"settings_key\" in awowlab.config.json, set "
           "\"settings_key_generated\" to false (or remove it), and configure again. "
           "Keep that file to yourself: it is gitignored and must never be committed.")
    if config.is_example:
        return (f"awowlab.config.json does not exist. Copy {os.path.basename(EXAMPLE_PATH)} "
                f"to it first. {how}")
    if not config.settings_key:
        return f"settings_key in {config.path} is empty. {how}"
    if config.settings_key_generated:
        return (f"settings_key in {config.path} was generated for this checkout, so a release "
                f"built with it could not read existing users' settings. {how}")
    return None


def write_public_key(hex_key, path=CONFIG_PATH):
    """Replace signing_public_key in the config, keeping the rest of the file as it is."""
    hex_key = check_public_key(hex_key)
    if not os.path.isfile(path):
        raise ConfigError(f"{path} does not exist yet: configure with CMake once to create it, "
                          f"or copy {os.path.basename(EXAMPLE_PATH)} to it")
    with open(path, encoding="utf-8", newline="") as handle:
        text = handle.read()
    updated, count = re.subn(r'("signing_public_key"\s*:\s*)"[^"]*"',
                             lambda m: f'{m.group(1)}"{hex_key}"', text)
    if count != 1:
        raise ConfigError(f"{path}: expected one signing_public_key entry, found {count}")
    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write(updated)
