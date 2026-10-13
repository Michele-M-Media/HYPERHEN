#!/usr/bin/env python3
"""Write edited JSONC translation catalogs back into web/index.html.

web/index.html keeps en-US and ru-RU as key/value objects inside
``translations`` and every other locale as a positional array inside
``translationRows`` indexed by ``translationKeys``. This tool reads the JSONC
files produced by tools/extract_web_l10n.py, validates them against the HTML,
and splices the values back into their original shape. Everything outside the
three localization blocks is left byte-for-byte identical.

Usage:
    python tools/merge_web_l10n.py ar-SA
    python tools/merge_web_l10n.py ru-RU --byte-units
    python tools/merge_web_l10n.py ar-SA ru-RU --dry-run
    python tools/merge_web_l10n.py --all --check

Nothing is written unless every requested locale validates.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from extract_web_l10n import (
    BYTE_UNIT_KEYS,
    BYTE_UNITS_SUFFIX,
    DEFAULT_HTML,
    DEFAULT_OUT_DIR,
    LOCALE_RE,
    Document,
    L10nError,
    closing_indent,
    js_string,
    parse_document,
    read_source,
    write_text,
)


def strip_line_comment(line: str) -> str:
    """Remove a // comment that sits outside any string literal."""
    in_string = False
    escaped = False
    for index, char in enumerate(line):
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "/" and line[index : index + 2] == "//":
            return line[:index]
    return line


def load_jsonc(path: Path) -> dict[str, str]:
    """Parse a JSONC catalog file into a plain key/value mapping."""
    try:
        text, _ = read_source(path)
    except L10nError as exc:
        raise L10nError(str(exc)) from exc
    stripped = "\n".join(strip_line_comment(line) for line in text.split("\n"))
    try:
        data = json.loads(stripped)
    except json.JSONDecodeError as exc:
        raise L10nError(f"{path.name} is not valid JSONC: {exc}") from exc
    if not isinstance(data, dict):
        raise L10nError(f"{path.name} must contain a single JSONC object")
    for key, value in data.items():
        if not isinstance(value, str):
            raise L10nError(f"{path.name}: {key} must be a string")
    return data


def render_object_locale(
    locale: str, pairs: list[tuple[str, str]], indent: str, key_indent: str
) -> str:
    lines = [f'{indent}"{locale}": {{']
    for key, value in pairs:
        lines.append(f"{key_indent}{key}: {js_string(value)},")
    lines[-1] = lines[-1][:-1]
    lines.append(indent + "}")
    return "\n".join(lines)


def render_array_row(
    locale: str, values: list[str], indent: str, separator: str
) -> str:
    inner = separator.join(js_string(value) for value in values)
    return f'{indent}"{locale}": [{inner}]'


def render_region(
    text: str, open_index: int, close_index: int, blocks: list[str]
) -> str:
    """Rebuild one block, keeping the original closing delimiter indentation."""
    return "{\n" + ",\n".join(blocks) + "\n" + closing_indent(text, close_index) + "}"


def resolve_locales(
    document: Document, requested: list[str], all_locales: bool, errors: list[str]
) -> list[str]:
    if all_locales:
        return document.locales
    valid: list[str] = []
    for locale in requested:
        if not LOCALE_RE.match(locale):
            errors.append(f"{locale}: not a locale such as ar-SA")
        elif (
            locale not in document.object_locales and locale not in document.row_locales
        ):
            errors.append(
                f"{locale}: unknown locale; available locales: "
                f"{', '.join(document.locales)}"
            )
        elif locale not in valid:
            valid.append(locale)
    return valid


def merge_catalog(
    document: Document, locale: str, path: Path, errors: list[str]
) -> list[tuple[str, str]] | None:
    """Validate one JSONC catalog and return the pairs to write back."""
    try:
        edited = load_jsonc(path)
    except L10nError as exc:
        errors.append(f"{locale}: {exc}")
        return None

    if locale in document.object_locales:
        current = document.object_locales[locale]
        required = [key for key, _ in current]
        unmergeable: list[str] = []
    else:
        current = list(zip(document.order, document.row_locales[locale]))
        required = list(document.order)
        unmergeable = document.orphan_keys

    known = {key for key, _ in current}
    allowed = known | set(unmergeable)
    unknown = sorted(set(edited) - allowed)
    if unknown:
        errors.append(f"{locale}: unknown keys: {unknown}")
        return None
    missing = [key for key in required if key not in edited]
    if missing:
        errors.append(f"{locale}: missing keys: {missing}")
        return None

    blocked = sorted(key for key in unmergeable if edited.get(key, "").strip())
    if blocked:
        errors.append(
            f"{locale}: {blocked} are absent from translationKeys, so "
            "translationRows has no slot for them and the value would be lost"
        )
        return None

    return [(key, edited[key]) for key, _ in current]


def merge_byte_units(
    document: Document, locale: str, path: Path, errors: list[str]
) -> list[str] | None:
    """Validate one JSONC byte unit catalog and return the values to write back."""
    try:
        edited = load_jsonc(path)
    except L10nError as exc:
        errors.append(f"{locale}: {exc}")
        return None

    unknown = sorted(set(edited) - set(BYTE_UNIT_KEYS))
    if unknown:
        errors.append(f"{locale}: unknown byte unit keys: {unknown}")
        return None
    missing = [key for key in BYTE_UNIT_KEYS if key not in edited]
    if missing:
        errors.append(f"{locale}: missing byte unit keys: {missing}")
        return None

    values = [edited[key] for key in BYTE_UNIT_KEYS]
    if locale not in document.byte_units:
        if not any(values):
            return None
        if not all(values):
            empty = sorted(
                BYTE_UNIT_KEYS[index] for index, value in enumerate(values) if not value
            )
            errors.append(
                f"{locale}: byteUnits has no entry for this locale, so either fill "
                f"in all {len(BYTE_UNIT_KEYS)} values or clear the file; empty: {empty}"
            )
            return None
    return values


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "locales",
        nargs="*",
        metavar="LOCALE",
        help="locale to merge, for example ar-SA",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        dest="all_locales",
        help="merge every locale that has a JSONC file",
    )
    parser.add_argument(
        "--byte-units",
        action="store_true",
        dest="byte_units",
        help=f"also merge <LOCALE>{BYTE_UNITS_SUFFIX}; with --all only locales "
        "that already define byte units are merged, while naming a locale "
        "requires the file and adds the locale to byteUnits if it is new",
    )
    parser.add_argument(
        "--html", type=Path, default=DEFAULT_HTML, help="target web/index.html"
    )
    parser.add_argument(
        "--l10n-dir",
        type=Path,
        default=DEFAULT_OUT_DIR,
        help="directory holding the JSONC files",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write anything; exit 1 if web/index.html is out of date",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        dest="dry_run",
        help="report what would change without writing",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.all_locales and args.locales:
        print("error: pass locales or --all, not both", file=sys.stderr)
        return 1
    if not args.all_locales and not args.locales:
        print("error: no locale given; pass a locale or --all", file=sys.stderr)
        return 1

    errors: list[str] = []
    try:
        text, newline = read_source(args.html)
        document = parse_document(text)
        locales = resolve_locales(document, args.locales, args.all_locales, errors)
    except L10nError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    object_pairs: dict[str, list[tuple[str, str]]] = {}
    row_values: dict[str, list[str]] = {}
    unit_values: dict[str, list[str]] = {}
    changed = 0

    for locale in locales:
        catalog_path = args.l10n_dir / f"{locale}.jsonc"
        if not catalog_path.is_file():
            errors.append(
                f"{locale}: {catalog_path} is missing; run "
                f"python tools/extract_web_l10n.py {locale} first"
            )
            continue

        pairs = merge_catalog(document, locale, catalog_path, errors)
        if pairs is None:
            continue
        if locale in document.object_locales:
            if pairs != document.object_locales[locale]:
                changed += 1
            object_pairs[locale] = pairs
        else:
            values = [value for _, value in pairs]
            if values != document.row_locales[locale]:
                changed += 1
            row_values[locale] = values

        if args.byte_units and (locale in document.byte_units or not args.all_locales):
            unit_path = args.l10n_dir / f"{locale}{BYTE_UNITS_SUFFIX}"
            if not unit_path.is_file():
                errors.append(
                    f"{locale}: {unit_path} is missing; run "
                    f"python tools/extract_web_l10n.py {locale} --byte-units first"
                )
                continue
            values = merge_byte_units(document, locale, unit_path, errors)
            if values is None:
                continue
            if document.byte_units.get(locale) != values:
                changed += 1
            unit_values[locale] = values

    if errors:
        print("Merge failed; web/index.html was not modified:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    edits: list[tuple[int, int, str]] = []

    def replace_region(name: str, blocks: list[str]) -> None:
        open_index, end_index = document.spans[name]
        close_index = end_index - 1
        edits.append(
            (
                open_index,
                end_index,
                render_region(text, open_index, close_index, blocks),
            )
        )

    if object_pairs:
        merged = dict(document.object_locales)
        merged.update(object_pairs)
        replace_region(
            "translations",
            [
                render_object_locale(
                    locale, merged[locale], *document.object_indents[locale]
                )
                for locale in merged
            ],
        )

    if row_values:
        merged = dict(document.row_locales)
        merged.update(row_values)
        replace_region(
            "translationRows",
            [
                render_array_row(
                    locale,
                    merged[locale],
                    document.row_indents[locale],
                    document.row_separator,
                )
                for locale in merged
            ],
        )

    if unit_values:
        merged = dict(document.byte_units)
        merged.update(unit_values)
        indent = next(iter(document.byte_unit_indents.values()))
        replace_region(
            "byteUnits",
            [
                render_array_row(
                    locale,
                    merged[locale],
                    document.byte_unit_indents.get(locale, indent),
                    document.unit_separator,
                )
                for locale in merged
            ],
        )

    if not edits:
        print("Nothing to merge.", file=sys.stderr)
        return 1

    result = text
    for start, end, replacement in sorted(edits, reverse=True):
        result = result[:start] + replacement + result[end:]

    if args.dry_run:
        print(f"Would update {changed} of {len(locales)} locale(s) in {args.html}")
        return 0
    if args.check:
        if result == text:
            print(f"{args.html} is up to date with {args.l10n_dir}")
            return 0
        print(
            f"error: {args.html} is out of date with {args.l10n_dir}", file=sys.stderr
        )
        return 1

    write_text(args.html, result, newline)
    print(
        f"Merged {len(locales)} locale(s), {changed} changed, into {args.html}\n"
        "Note: web/index.html is embedded into src/web_index_asset.c; rebuild "
        "before deploying to the console."
    )
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    raise SystemExit(main())
