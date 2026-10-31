#!/usr/bin/env python3
"""
AwowLab Localization - DeepL Translation Script

Fills in the strings each locale file is missing, using the DeepL API.
A string counts as missing when its key is absent or its value is still the
English text (sync_missing.py puts English in as a placeholder). Everything a
translator has already written is kept. Format placeholders (%s, %d, %.1f%%,
etc.) are preserved.

Usage:
    1. Get a free DeepL API key from https://www.deepl.com/pro-api
    2. Put your key in deepl_api_key in awowlab.config.json at the repository root
    3. Run: python translate_all.py          # fill gaps in every locale
            python translate_all.py de_DE    # one locale
            python translate_all.py --all    # retranslate everything
"""

import csv
import html
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "release"))
import awowlab_config  # noqa: E402

try:
    import deepl
except ImportError:
    print("Error: deepl package not installed.")
    print("Run: pip install deepl")
    sys.exit(1)


# Locale mapping: our locale code -> DeepL target language code
LOCALES = {
    "de_DE": "DE",
    "fr_FR": "FR",
    "es_MX": "ES",       # Mexico uses same Spanish as Spain in DeepL
    "pt_BR": "PT-BR",
    "ru_RU": "RU",
    "ko_KR": "KO",
    "zh_CN": "ZH-HANS",  # Simplified Chinese
    "zh_TW": "ZH-HANT",  # Traditional Chinese
}

# Language display names for progress output
LOCALE_NAMES = {
    "de_DE": "German",
    "fr_FR": "French",
    "es_MX": "Spanish (Mexico)",
    "pt_BR": "Portuguese (Brazil)",
    "ru_RU": "Russian",
    "ko_KR": "Korean",
    "zh_CN": "Chinese (Simplified)",
    "zh_TW": "Chinese (Traditional)",
}

# Regex to find printf-style placeholders
PLACEHOLDER_PATTERN = re.compile(
    r'%(?:'
    r'[-+0#]*'            # Flags (no space: "200% crit" is text, not "% c")
    r'(?:\d+|\*)?'        # Width
    r'(?:\.(?:\d+|\*))?'  # Precision
    r'(?:hh|h|l|ll|j|z|t|L)?'  # Length modifier
    r'[diuoxXeEfFgGaAcspn%]'   # Conversion specifier
    r')'
)


def deepl_key():
    """The DeepL key from awowlab.config.json, or exit saying where it goes."""
    try:
        key = awowlab_config.load().deepl_api_key
    except awowlab_config.ConfigError as error:
        print(f"Error: {error}")
        sys.exit(1)
    if not key:
        print("Error: no DeepL API key. Put it in deepl_api_key in awowlab.config.json")
        print("at the repository root (gitignored; never commit it).")
        sys.exit(1)
    return key


def placeholders_in(text):
    return [m.group(0) for m in PLACEHOLDER_PATTERN.finditer(text)]


def protect_placeholders(text):
    """
    Wrap each printf placeholder in an ignored XML tag. DeepL leaves the
    contents alone and keeps it attached to the words around it; an empty tag
    tends to drift past the next word ("in %d min" came back "in  min%d").
    Returns (modified_text, list_of_placeholders).
    """
    placeholders = []

    def replace(match):
        placeholders.append(match.group(0))
        return f'<x i="{len(placeholders) - 1}">{match.group(0)}</x>'

    protected = PLACEHOLDER_PATTERN.sub(replace, html.escape(text, quote=False))
    return protected, placeholders


def restore_placeholders(text, placeholders):
    """Put the original placeholders back in place of the tags."""
    def replace(match):
        index = int(match.group(1))
        return placeholders[index] if index < len(placeholders) else match.group(0)

    return html.unescape(re.sub(r'<x\s+i="(\d+)"\s*>.*?</x>', replace, text))


def parse_csv(filepath):
    """
    Parse the localization CSV file.
    Returns list of tuples: (line_type, content)
    - ("comment", "# comment text")
    - ("empty", "")
    - ("header", "key,value")
    - ("entry", (key, value))
    """
    entries = []

    with open(filepath, "r", encoding="utf-8-sig") as f:
        for line in f:
            line = line.rstrip("\r\n")

            if not line:
                entries.append(("empty", ""))
            elif line.startswith("#"):
                entries.append(("comment", line))
            elif line == "key,value":
                entries.append(("header", line))
            else:
                # Parse key,value - handle quoted values
                if "," in line:
                    comma_idx = line.index(",")
                    key = line[:comma_idx]
                    value = line[comma_idx + 1:]
                    # Remove surrounding quotes if present
                    if value.startswith('"') and value.endswith('"'):
                        value = value[1:-1].replace('""', '"')
                    entries.append(("entry", (key, value)))
                else:
                    # Malformed line, treat as comment
                    entries.append(("comment", f"# MALFORMED: {line}"))

    return entries


def existing_translations(filepath):
    """Key -> value for a locale file, or an empty dict if there is none."""
    if not filepath.exists():
        return {}
    return {key: value for line_type, content in parse_csv(filepath)
            if line_type == "entry" for key, value in [content]}


def translate_entries(entries, translator, target_lang, locale_name, existing):
    """
    Return the English entries with each value replaced by its translation:
    the one in `existing` when it holds a real translation, a new DeepL one
    otherwise.
    """
    translated = []

    to_translate = []
    indices = []

    for i, (line_type, content) in enumerate(entries):
        if line_type == "entry":
            key, value = content
            kept = existing.get(key)
            if kept and kept != value:
                translated.append(("entry", (key, kept)))
                continue
            if value:  # Skip empty values
                to_translate.append(value)
                indices.append(i)
        translated.append((line_type, content))

    if not to_translate:
        print(f"  Nothing missing in {locale_name}")
        return translated

    print(f"  Translating {len(to_translate)} strings to {locale_name}...")

    # Protect placeholders before translation
    protected_texts = []
    all_placeholders = []

    for text in to_translate:
        protected, placeholders = protect_placeholders(text)
        protected_texts.append(protected)
        all_placeholders.append(placeholders)

    # Batch translate with DeepL
    try:
        results = translator.translate_text(
            protected_texts,
            source_lang="EN",
            target_lang=target_lang,
            preserve_formatting=True,
            tag_handling="xml",
            ignore_tags=["x"],
        )
    except deepl.DeepLException as e:
        print(f"  Error translating to {locale_name}: {e}")
        return None

    for idx, result, placeholders in zip(indices, results, all_placeholders):
        line_type, (key, _) = translated[idx]
        translated_text = restore_placeholders(result.text, placeholders)
        if placeholders_in(translated_text) != placeholders:
            # A reordered or lost placeholder would break the format call;
            # keep English so the next run tries this string again.
            print(f"  Kept English for {key}: placeholders changed")
            continue
        translated[idx] = ("entry", (key, translated_text))

    return translated


def write_csv(entries, filepath, locale_code):
    """Write translated entries to CSV file."""
    # Get language name for header
    lang_name = LOCALE_NAMES.get(locale_code, locale_code)

    with open(filepath, "w", encoding="utf-8", newline="") as f:
        # Write header comment
        f.write(f"# AwowLab Localization - {lang_name}\n")
        f.write("# Machine translated with DeepL - community review welcome\n")
        f.write("# Format: key,value\n")
        f.write("# Lines starting with # are comments\n")
        f.write("key,value\n")
        f.write("\n")

        # Skip original header section (first 4 lines typically)
        skip_header = True

        for line_type, content in entries:
            if line_type == "header":
                skip_header = False
                continue  # Skip original header, we wrote our own

            if skip_header and line_type in ("comment", "empty"):
                continue  # Skip original file header comments

            if line_type == "empty":
                f.write("\n")
            elif line_type == "comment":
                f.write(f"{content}\n")
            elif line_type == "entry":
                key, value = content
                # Quote value if it contains commas or quotes
                if "," in value or '"' in value:
                    value = '"' + value.replace('"', '""') + '"'
                f.write(f"{key},{value}\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    retranslate_all = "--all" in sys.argv
    unknown = [a for a in args if a not in LOCALES]
    if unknown:
        print(f"Unknown locale: {', '.join(unknown)} (known: {', '.join(LOCALES)})")
        sys.exit(1)
    locales = {code: LOCALES[code] for code in (args or LOCALES)}

    # Paths
    script_dir = Path(__file__).parent
    source_file = script_dir.parent.parent / "AwowLab" / "data" / "lang" / "en_US.csv"
    output_dir = source_file.parent

    print("AwowLab Localization Translator")
    print("=" * 40)

    key = deepl_key()

    # Initialize DeepL translator
    print("\nInitializing DeepL API...")
    translator = deepl.Translator(key)

    # Check API usage
    try:
        usage = translator.get_usage()
        if usage.character.limit:
            remaining = usage.character.limit - usage.character.count
            print(f"API Usage: {usage.character.count:,} / {usage.character.limit:,} characters")
            print(f"Remaining: {remaining:,} characters")
    except Exception as e:
        print(f"Warning: Could not check API usage: {e}")

    # Parse source file
    print(f"\nReading source: {source_file}")
    if not source_file.exists():
        print(f"Error: Source file not found: {source_file}")
        sys.exit(1)

    entries = parse_csv(source_file)
    entry_count = sum(1 for t, _ in entries if t == "entry")
    print(f"Found {entry_count} translatable strings")

    print(f"\nTranslating to {len(locales)} locales"
          + (" (everything)..." if retranslate_all else " (missing strings only)..."))
    print("-" * 40)

    success_count = 0
    for locale_code, deepl_code in locales.items():
        locale_name = LOCALE_NAMES[locale_code]
        output_file = output_dir / f"{locale_code}.csv"

        print(f"\n[{locale_code}] {locale_name}")

        existing = {} if retranslate_all else existing_translations(output_file)
        translated = translate_entries(entries, translator, deepl_code, locale_name, existing)

        if translated:
            write_csv(translated, output_file, locale_code)
            print(f"  Wrote: {output_file.name}")
            success_count += 1
        else:
            print(f"  FAILED - skipping {locale_code}")

    print("\n" + "=" * 40)
    print(f"Completed: {success_count}/{len(locales)} locales translated")
    print(f"Output directory: {output_dir}")

    # Final usage check
    try:
        usage = translator.get_usage()
        if usage.character.limit:
            print(f"\nFinal API usage: {usage.character.count:,} / {usage.character.limit:,}")
    except:
        pass


if __name__ == "__main__":
    main()
