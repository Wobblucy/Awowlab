#!/usr/bin/env python3
"""
Convert spellname.txt from WoW DB2 export to compressed CSV format.

Input format (spellname.txt):
    Layout: 782ee721
    1: Word of Recall (OLD)
    17: Power Word: Shield
    ...

Output format (spell_names_{locale}.csv.gz):
    spell_id,{locale}
    1,Word of Recall (OLD)
    17,Power Word: Shield
    ...

Usage:
    # Default (English, reads spellname from AWOWLAB_DB2_DIR, else rawdb2/<version>)
    python convert_spellnames.py

    # Korean spell names
    python convert_spellnames.py "<koKR exports>/spellname.txt" --locale ko_KR

    # Custom output path
    python convert_spellnames.py input.txt --locale de_DE --output custom_path.csv.gz
"""

import argparse
import gzip
import io
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import find_export, resolve_db2_dir

# Supported locales (matching LocalizationManager.h)
VALID_LOCALES = [
    'en_US',  # English (US) - Default
    'es_MX',  # Spanish (Mexico)
    'pt_BR',  # Portuguese (Brazil)
    'de_DE',  # German
    'fr_FR',  # French
    'ru_RU',  # Russian
    'ko_KR',  # Korean
    'zh_TW',  # Chinese (Traditional)
    'zh_CN',  # Chinese (Simplified)
]

def parse_spellname_file(input_path: Path) -> dict[int, str]:
    """Parse spellname.txt and return dict of spell_id -> name."""
    spells = {}
    pattern = re.compile(r'^(\d+):\s*(.+)$')

    with open(input_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue

            match = pattern.match(line)
            if match:
                spell_id = int(match.group(1))
                name = match.group(2)
                spells[spell_id] = name

    return spells

def write_compressed_csv(spells: dict[int, str], output_path: Path, locale: str):
    """Write spells to gzip-compressed CSV with locale header.

    The gzip header normally records the file name and the time of writing, so
    two runs over the same export would differ. Leaving both out makes the
    output depend on the spells alone. Lines end in CRLF, as the shipped file
    always has, whatever platform this runs on.
    """
    with open(output_path, 'wb') as raw, \
            gzip.GzipFile(filename='', mode='wb', fileobj=raw, compresslevel=9, mtime=0) as packed, \
            io.TextIOWrapper(packed, encoding='utf-8', newline='\r\n') as f:
        f.write(f'spell_id,{locale}\n')
        for spell_id in sorted(spells.keys()):
            name = spells[spell_id]
            # Escape commas and quotes in names
            if ',' in name or '"' in name:
                name = '"' + name.replace('"', '""') + '"'
            f.write(f'{spell_id},{name}\n')

def main():
    parser = argparse.ArgumentParser(
        description='Convert WoW DB2 spell name export to compressed CSV format.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                                    # English from default path
  %(prog)s input.txt --locale ko_KR           # Korean spell names
  %(prog)s input.txt -l de_DE -o output.csv.gz  # German with custom output
        """
    )
    parser.add_argument(
        'input',
        nargs='?',
        help='Input spellname.txt file (default: spellname from AWOWLAB_DB2_DIR, else rawdb2/<version>)'
    )
    parser.add_argument(
        '-l', '--locale',
        default='en_US',
        choices=VALID_LOCALES,
        help='Locale code for output (default: en_US)'
    )
    parser.add_argument(
        '-o', '--output',
        help='Output file path (default: AwowLab/data/spellnames/spell_names_{locale}.csv.gz)'
    )

    args = parser.parse_args()

    # Determine paths
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent.parent

    # Input path
    if args.input:
        input_path = Path(args.input)
    else:
        db2_dir = resolve_db2_dir()
        input_path = find_export(db2_dir, 'spellname') or db2_dir / 'spellname.txt'

    # Output path
    if args.output:
        output_path = Path(args.output)
    else:
        # For en_US, output to data/spellnames (included in builds)
        # For other locales, output to optional_packs/{locale}/ (downloaded via updater)
        if args.locale == 'en_US':
            output_dir = repo_root / 'AwowLab' / 'data' / 'spellnames'
            output_path = output_dir / 'spell_names.csv.gz'
        else:
            output_dir = repo_root / 'optional_packs' / args.locale
            output_path = output_dir / f'spell_names_{args.locale}.csv.gz'

    if not input_path.exists():
        print(f'Error: Input file not found: {input_path}')
        sys.exit(1)

    print(f'Locale: {args.locale}')
    print(f'Reading: {input_path}')
    spells = parse_spellname_file(input_path)
    print(f'Parsed {len(spells):,} spell names')

    # Create output directory if needed
    output_path.parent.mkdir(parents=True, exist_ok=True)

    print(f'Writing: {output_path}')
    write_compressed_csv(spells, output_path, args.locale)

    # Report sizes
    input_size = input_path.stat().st_size
    output_size = output_path.stat().st_size
    ratio = (1 - output_size / input_size) * 100

    print(f'Input size:  {input_size:,} bytes ({input_size / 1024 / 1024:.2f} MB)')
    print(f'Output size: {output_size:,} bytes ({output_size / 1024 / 1024:.2f} MB)')
    print(f'Compression: {ratio:.1f}% reduction')

if __name__ == '__main__':
    main()
