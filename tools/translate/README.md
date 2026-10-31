# AwowLab Translation Tools

The language files in `AwowLab/data/lang/` (`key,value` CSVs, one per locale)
are the source of truth. Translators improve them by pull request; see the
Translations section of `CONTRIBUTING.md`. These scripts fill the gaps:

1. **`sync_missing.py`** - Propagates new English strings to all locales (no API needed)
2. **`translate_all.py`** - Fills in missing translations using the DeepL API

## Quick Workflow

```bash
# 1. Add new strings to en_US.csv during development

# 2. Sync to other locales (uses English as placeholder)
python sync_missing.py

# 3. Translate new strings when ready
python translate_all.py
```

## Prerequisites

- Python 3.8+
- DeepL API key (free tier available)

## Setup

1. **Install dependencies:**
   ```bash
   pip install -r requirements.txt
   ```

2. **Get a DeepL API key:**
   - Go to https://www.deepl.com/pro-api
   - Sign up for a free account (500,000 characters/month)
   - Copy your API key from the account dashboard

3. **Configure the API key:**
   - Edit `config.json`
   - Replace `YOUR_API_KEY_HERE` with your actual API key

## Usage

```bash
cd tools/translate
python translate_all.py          # fill gaps in every locale
python translate_all.py de_DE    # one locale
python translate_all.py --all    # retranslate everything, discarding edits
```

The script reads `AwowLab/data/lang/en_US.csv` and, for each locale, sends
DeepL only the strings that file is missing: keys it doesn't have, and values
that are still the English text (the placeholders `sync_missing.py` adds).
Translations already in the file, including ones people corrected by hand,
are kept. Each locale file is rewritten in `en_US.csv`'s order, so review the
diff before committing.

## Supported Locales

| Locale | Language |
|--------|----------|
| de_DE | German |
| fr_FR | French |
| es_MX | Spanish (Mexico) |
| pt_BR | Portuguese (Brazil) |
| ru_RU | Russian |
| ko_KR | Korean |
| zh_CN | Chinese (Simplified) |
| zh_TW | Chinese (Traditional) |

## API Usage

- A normal run costs only the missing strings. `--all` sends every string to
  all 8 locales, which can exceed the free tier
- Free tier: 500,000 characters/month

## Placeholder Handling

The script preserves printf-style placeholders during translation:
- `%s`, `%d`, `%u`, `%f`, `%.1f%%`, `%zu`, etc.

Example:
```
English: "Total: %s | DPS: %s"
German:  "Gesamt: %s | DPS: %s"
```

## Output Format

Translated files include a header indicating machine translation:
```csv
# AwowLab Localization - German
# Machine translated with DeepL - community review welcome
# Format: key,value
key,value
```

## Troubleshooting

**API key error:**
- Ensure `config.json` contains your actual API key
- Check that the key is valid at https://www.deepl.com/account

**Rate limit error:**
- Wait until the next month for free tier reset
- Or upgrade to a paid DeepL plan

**Missing translations:**
- Check the console output for error messages
- Some strings may fail due to special characters
