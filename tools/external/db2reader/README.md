# db2reader

Upstream: <https://github.com/erorus/db2> (Apache-2.0)
Base commit: `1dcc652` (WDC5 support)

Converts the game's raw `.db2` tables into the text exports every script under
`tools/` reads: a `Layout: <hash>` line followed by one `id: field,field,...`
line per record. This is step one of a game version update; see
`docs/UPDATE-FLOW.md`.

Needs PHP 8.3 or later on `PATH`.

## Patch

`patches/0001-example-export-every-record.patch` removes the 10-record limit
from upstream's `example.php`, so it converts a whole table instead of printing
a sample. It is a plain diff; apply it with `git apply`.

## Script

`scripts/export_batch.php` converts every `.db2` in a folder in one run. Copy it
into the db2reader checkout next to `example.php`; it loads the library from
`src/` beside it.

## Usage

```bash
git clone https://github.com/erorus/db2 && cd db2
git checkout 1dcc652
git apply /path/to/AwowLab/tools/external/db2reader/patches/*.patch
cp /path/to/AwowLab/tools/external/db2reader/scripts/export_batch.php .

# One table
php example.php <dump>/uimap.db2 > <exports>/uimap.txt

# A whole folder, keeping each table's own name ({table}.txt)
php export_batch.php <dump-folder> <exports-folder> --keep-name
```

Pass forward-slash paths when calling PHP from bash.
