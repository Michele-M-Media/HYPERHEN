#!/usr/bin/env python3
"""Export one web UI translation catalog from web/index.html to JSONC.

The web UI stores its translations in two shapes: ``en-US`` and ``ru-RU`` are
key/value objects inside ``translations``, while every other locale is a
positional array inside ``translationRows`` indexed by ``translationKeys``.

Both shapes are normalized into a single key/value JSONC file so translators
work with readable keys instead of array offsets, and each value is preceded by
a ``// English:`` comment taken from the ``en-US`` catalog.

Usage:
    python tools/extract_web_l10n.py ar-SA
    python tools/extract_web_l10n.py ru-RU --byte-units
    python tools/extract_web_l10n.py --all

Run tools/merge_web_l10n.py to write the edited JSONC back into web/index.html.
"""

from __future__ import annotations

import argparse
import dataclasses
import itertools
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HTML = ROOT / "web" / "index.html"
DEFAULT_OUT_DIR = ROOT / "web" / "l10n"

BASE_LOCALE = "en-US"
BYTE_UNITS_SUFFIX = ".byte-units.jsonc"
BYTE_UNIT_KEYS = ("bytes", "kibibytes", "mebibytes", "gibibytes", "tebibytes")
BYTE_UNIT_BASE_VALUES = ("B", "KiB", "MiB", "GiB", "TiB")

LOCALE_RE = re.compile(r"^[a-z]{2}-[A-Z]{2}$")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')
OPTION_RE = re.compile(r'<option value="([a-z]{2}-[A-Z]{2})">([^<]*)</option>')
PAIRS = {"{": "}", "[": "]"}
IDENTIFIER_START = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$")
IDENTIFIER_BODY = IDENTIFIER_START | set("0123456789")
SIMPLE_ESCAPES = {
    "b": "\b",
    "f": "\f",
    "n": "\n",
    "r": "\r",
    "t": "\t",
    "v": "\v",
    "0": "\0",
}


class L10nError(Exception):
    """Raised when web/index.html cannot be parsed."""


@dataclasses.dataclass
class JsEntry:
    """One ``key: value`` pair inside a JavaScript object literal."""

    key: str
    key_start: int
    start: int
    end: int
    indent: str


@dataclasses.dataclass
class JsBlock:
    """A locale entry whose value is itself an object or array literal."""

    name: str
    start: int
    end: int
    indent: str
    entries: list[JsEntry]


@dataclasses.dataclass
class Document:
    """The parsed localization state of web/index.html."""

    text: str
    order: list[str]
    object_locales: dict[str, list[tuple[str, str]]]
    object_indents: dict[str, tuple[str, str]]
    row_locales: dict[str, list[str]]
    row_indents: dict[str, str]
    byte_units: dict[str, list[str]]
    byte_unit_indents: dict[str, str]
    row_separator: str
    unit_separator: str
    labels: dict[str, str]
    spans: dict[str, tuple[int, int]]

    @property
    def locales(self) -> list[str]:
        return [*self.object_locales, *self.row_locales]

    @property
    def orphan_keys(self) -> list[str]:
        """Keys present in the object-form locales but absent from translationKeys."""
        base = self.object_locales.get(BASE_LOCALE, [])
        return [key for key, _ in base if key not in self.order]


def read_source(path: Path) -> tuple[str, str]:
    """Return the file contents normalized to LF plus its original line ending."""
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise L10nError(f"cannot read {path}: {exc}") from exc
    try:
        if raw.startswith(b"\xef\xbb\xbf"):
            raw = raw[3:]
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise L10nError(f"{path} is not valid UTF-8: {exc}") from exc
    newline = "\r\n" if "\r\n" in text else "\n"
    return text.replace("\r\n", "\n"), newline


def write_text(path: Path, text: str, newline: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\n", newline).encode("utf-8"))


def _skip_string(text: str, index: int) -> int:
    """Return the offset just past the string literal starting at ``index``."""
    quote = text[index]
    cursor = index + 1
    limit = len(text)
    while cursor < limit:
        char = text[cursor]
        if char == "\\":
            cursor += 2
            continue
        if char == quote:
            return cursor + 1
        cursor += 1
    raise L10nError(f"unterminated {quote} string at offset {index}")


def _skip_comment(text: str, index: int) -> int:
    if text[index : index + 2] == "//":
        end = text.find("\n", index)
        return len(text) if end < 0 else end + 1
    end = text.find("*/", index + 2)
    if end < 0:
        raise L10nError(f"unterminated block comment at offset {index}")
    return end + 2


def _skip_whitespace(text: str, index: int) -> int:
    cursor = index
    limit = len(text)
    while cursor < limit:
        if text[cursor].isspace():
            cursor += 1
        elif text[cursor : cursor + 2] in ("//", "/*"):
            cursor = _skip_comment(text, cursor)
        else:
            break
    return cursor


def find_block(text: str, start: int) -> int:
    """Return the offset of the delimiter closing the block at ``start``."""
    if text[start] not in PAIRS:
        raise L10nError(f"expected a block at offset {start}, found {text[start]!r}")
    stack = [PAIRS[text[start]]]
    cursor = start + 1
    limit = len(text)
    while cursor < limit and stack:
        char = text[cursor]
        if char in "\"'`":
            cursor = _skip_string(text, cursor)
            continue
        if char == "/" and text[cursor : cursor + 2] in ("//", "/*"):
            cursor = _skip_comment(text, cursor)
            continue
        if char in PAIRS:
            stack.append(PAIRS[char])
        elif char in ("}", "]"):
            if char != stack[-1]:
                raise L10nError(f"mismatched {char!r} at offset {cursor}")
            stack.pop()
        cursor += 1
    if stack:
        raise L10nError(f"unterminated block at offset {start}")
    return cursor - 1


def decode_js_string(token: str) -> str:
    """Decode a JavaScript string literal into a Python string."""
    quote = token[0]
    if quote == '"':
        try:
            return json.loads(token)
        except json.JSONDecodeError as exc:
            raise L10nError(f"invalid string literal: {exc}") from exc
    if quote not in ("'", "`"):
        raise L10nError(f"expected a string literal, found {token[:32]!r}")
    body = token[1:-1]
    out: list[str] = []
    cursor = 0
    while cursor < len(body):
        char = body[cursor]
        if char != "\\":
            out.append(char)
            cursor += 1
            continue
        cursor += 1
        if cursor >= len(body):
            raise L10nError("dangling escape in string literal")
        escape = body[cursor]
        if escape == "u":
            if body[cursor + 1 : cursor + 2] == "{":
                end = body.index("}", cursor)
                out.append(chr(int(body[cursor + 2 : end], 16)))
                cursor = end + 1
            else:
                out.append(chr(int(body[cursor + 1 : cursor + 5], 16)))
                cursor += 5
            continue
        if escape == "x":
            out.append(chr(int(body[cursor + 1 : cursor + 3], 16)))
            cursor += 3
            continue
        out.append(SIMPLE_ESCAPES.get(escape, escape))
        cursor += 1
    return "".join(out)


def string_separator(text: str, start: int, end: int) -> str:
    """Return the separator used between array items, "," or ", "."""
    matches = list(STRING_RE.finditer(text, start, end))
    for previous, current in itertools.pairwise(matches):
        gap = text[previous.end() : current.start()]
        if gap in (",", ", "):
            return gap
    return ","


def js_string(value: str) -> str:
    """Encode a Python string as a double-quoted JavaScript string literal."""
    return json.dumps(value, ensure_ascii=False)


def line_indent(text: str, index: int) -> str:
    """Return the whitespace between the start of the line and ``index``."""
    start = text.rfind("\n", 0, index) + 1
    prefix = text[start:index]
    return prefix if prefix.strip() == "" else ""


def closing_indent(text: str, close: int) -> str:
    """Return the indentation of the line holding the closing delimiter."""
    start = text.rfind("\n", 0, close) + 1
    prefix = text[start:close]
    if prefix.strip():
        raise L10nError(f"closing delimiter at offset {close} is not on its own line")
    return prefix


def value_end(text: str, index: int) -> int:
    """Return the offset just past the value starting at ``index``."""
    char = text[index]
    if char in PAIRS:
        return find_block(text, index) + 1
    if char in "\"'`":
        return _skip_string(text, index)
    cursor = index
    while cursor < len(text) and text[cursor] not in ",}]:\n \t":
        cursor += 1
    return cursor


def object_entries(text: str, open_index: int) -> tuple[list[JsEntry], int]:
    """Parse the top-level ``key: value`` pairs of an object literal."""
    close = find_block(text, open_index)
    entries: list[JsEntry] = []
    cursor = open_index + 1
    while cursor < close:
        char = text[cursor]
        if char.isspace() or char == ",":
            cursor += 1
            continue
        if char == "/" and text[cursor : cursor + 2] in ("//", "/*"):
            cursor = _skip_comment(text, cursor)
            continue
        if char in "\"'`" or char in IDENTIFIER_START:
            key_start = cursor
            if char in "\"'`":
                key_end = _skip_string(text, cursor)
                key = decode_js_string(text[key_start:key_end])
                cursor = key_end
            else:
                cursor += 1
                while cursor < close and text[cursor] in IDENTIFIER_BODY:
                    cursor += 1
                key = text[key_start:cursor]
            cursor = _skip_whitespace(text, cursor)
            if cursor >= close or text[cursor] != ":":
                raise L10nError(f"expected ':' after key {key!r} at offset {cursor}")
            cursor = _skip_whitespace(text, cursor + 1)
            start = cursor
            end = value_end(text, cursor)
            entries.append(
                JsEntry(key, key_start, start, end, line_indent(text, key_start))
            )
            cursor = end
            continue
        raise L10nError(
            f"unexpected {char!r} at offset {cursor} inside an object literal"
        )
    return entries, close


def const_value_offset(text: str, name: str) -> int:
    """Return the offset of the value assigned to ``const <name>``."""
    match = re.search(rf"\bconst\s+{re.escape(name)}\s*=\s*", text)
    if not match:
        raise L10nError(f"const {name} not found in web/index.html")
    offset = match.end()
    if offset >= len(text) or text[offset] not in PAIRS:
        raise L10nError(f"const {name} is not initialized with a block")
    return offset


def parse_document(text: str) -> Document:
    """Parse every localization structure out of web/index.html."""
    keys_open = const_value_offset(text, "translationKeys")
    keys_close = find_block(text, keys_open)
    order = [
        decode_js_string(token)
        for token in STRING_RE.findall(text[keys_open + 1 : keys_close])
    ]
    if len(set(order)) != len(order):
        raise L10nError("translationKeys contains duplicate entries")

    translations_open = const_value_offset(text, "translations")
    translations_close = find_block(text, translations_open)
    translations, _ = object_entries(text, translations_open)

    object_locales: dict[str, list[tuple[str, str]]] = {}
    object_indents: dict[str, tuple[str, str]] = {}
    for entry in translations:
        if text[entry.start] != "{":
            raise L10nError(
                f"translations[{entry.key}] must be an object literal, found "
                f"{text[entry.start]!r}"
            )
        inner, _ = object_entries(text, entry.start)
        pairs: list[tuple[str, str]] = []
        for item in inner:
            if text[item.start] not in "\"'`":
                raise L10nError(
                    f"translations[{entry.key}].{item.key} must be a string literal"
                )
            pairs.append((item.key, decode_js_string(text[item.start : item.end])))
        duplicates = [key for key, _ in pairs if [k for k, _ in pairs].count(key) > 1]
        if duplicates:
            raise L10nError(
                f"translations[{entry.key}] has duplicate keys: {sorted(set(duplicates))}"
            )
        key_indent = inner[0].indent if inner else entry.indent + "  "
        object_locales[entry.key] = pairs
        object_indents[entry.key] = (entry.indent, key_indent)

    rows_open = const_value_offset(text, "translationRows")
    rows_close = find_block(text, rows_open)
    rows, _ = object_entries(text, rows_open)

    row_locales: dict[str, list[str]] = {}
    row_indents: dict[str, str] = {}
    for entry in rows:
        if text[entry.start] != "[":
            raise L10nError(
                f"translationRows[{entry.key}] must be an array literal, found "
                f"{text[entry.start]!r}"
            )
        values = [
            decode_js_string(token)
            for token in STRING_RE.findall(
                text[entry.start + 1 : find_block(text, entry.start)]
            )
        ]
        if len(values) != len(order):
            raise L10nError(
                f"translationRows[{entry.key}] has {len(values)} values but "
                f"translationKeys has {len(order)}"
            )
        row_locales[entry.key] = values
        row_indents[entry.key] = entry.indent

    units_open = const_value_offset(text, "byteUnits")
    units_close = find_block(text, units_open)
    units, _ = object_entries(text, units_open)

    byte_units: dict[str, list[str]] = {}
    byte_unit_indents: dict[str, str] = {}
    for entry in units:
        if text[entry.start] != "[":
            raise L10nError(
                f"byteUnits[{entry.key}] must be an array literal, found "
                f"{text[entry.start]!r}"
            )
        values = [
            decode_js_string(token)
            for token in STRING_RE.findall(
                text[entry.start + 1 : find_block(text, entry.start)]
            )
        ]
        if len(values) != len(BYTE_UNIT_KEYS):
            raise L10nError(
                f"byteUnits[{entry.key}] has {len(values)} values but "
                f"{len(BYTE_UNIT_KEYS)} were expected"
            )
        byte_units[entry.key] = values
        byte_unit_indents[entry.key] = entry.indent

    base_units = byte_units.get(BASE_LOCALE)
    if base_units is not None and tuple(base_units) != BYTE_UNIT_BASE_VALUES:
        raise L10nError(
            f"byteUnits[{BASE_LOCALE}] is {base_units}, expected "
            f"{list(BYTE_UNIT_BASE_VALUES)}"
        )

    labels = dict(OPTION_RE.findall(text))

    return Document(
        text=text,
        order=order,
        object_locales=object_locales,
        object_indents=object_indents,
        row_locales=row_locales,
        row_indents=row_indents,
        byte_units=byte_units,
        byte_unit_indents=byte_unit_indents,
        row_separator=string_separator(text, rows_open, rows_close),
        unit_separator=string_separator(text, units_open, units_close),
        labels=labels,
        spans={
            "translations": (translations_open, translations_close + 1),
            "translationRows": (rows_open, rows_close + 1),
            "byteUnits": (units_open, units_close + 1),
        },
    )


def english_comment(value: str) -> str:
    """Escape a value so it is safe inside a single-line // comment."""
    return (
        value.replace("\\", "\\\\")
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )


def render_catalog(
    document: Document,
    locale: str,
    pairs: list[tuple[str, str]],
    english: dict[str, str],
    title: str,
    command: str,
) -> str:
    """Render one locale as JSONC with an English comment above every value."""
    lines = [
        f"// ShadowMount+ web UI {title} - {locale}",
        f"// {document.labels.get(locale, 'Unknown language')}",
        "//",
        "// Generated by tools/extract_web_l10n.py from web/index.html.",
        f"// Edit the values, then run:  {command}",
        "//",
        f'// Each entry is preceded by its "{BASE_LOCALE}" reference.',
        "// Keep placeholders such as {title} or {shown} exactly as written.",
        "",
        "{",
    ]
    for key, value in pairs:
        reference = english.get(key)
        if reference is not None:
            lines.append(f"  // English: {english_comment(reference)}")
        lines.append(f"  {js_string(key)}: {js_string(value)},")
    lines[-1] = lines[-1][:-1]
    lines.append("}")
    return "\n".join(lines) + "\n"


def byte_unit_pairs(values: list[str] | None) -> list[tuple[str, str]]:
    if values is None:
        return [(key, "") for key in BYTE_UNIT_KEYS]
    return list(zip(BYTE_UNIT_KEYS, values))


def catalog_pairs(document: Document, locale: str) -> list[tuple[str, str]]:
    """Return the key/value pairs of ``locale``.

    Array locales deliberately omit the keys that exist only in the object-form
    locales. ``translationKeys`` has no slot for them, so a value could never be
    written back; emitting empty entries would only invite a translator to fill
    in something that would then be rejected. They stay English-only.
    """
    if locale in document.object_locales:
        return list(document.object_locales[locale])
    return list(zip(document.order, document.row_locales[locale]))


def english_reference(document: Document) -> dict[str, str]:
    base = document.object_locales.get(BASE_LOCALE)
    if base is None:
        raise L10nError(f'translations["{BASE_LOCALE}"] is required as the reference')
    return dict(base)


def byte_unit_reference(document: Document) -> dict[str, str]:
    values = document.byte_units.get(BASE_LOCALE)
    if values is None:
        raise L10nError(f'byteUnits["{BASE_LOCALE}"] is required as the reference')
    return dict(zip(BYTE_UNIT_KEYS, values))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "locales",
        nargs="*",
        metavar="LOCALE",
        help=f"locale to export, for example {BASE_LOCALE} or ar-SA",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        dest="all_locales",
        help="export every locale found in web/index.html",
    )
    parser.add_argument(
        "--byte-units",
        action="store_true",
        dest="byte_units",
        help=f"also export the byte unit catalog as <LOCALE>{BYTE_UNITS_SUFFIX}; "
        "with --all only locales that already define byte units are exported, "
        "while naming a locale exports a blank template to fill in",
    )
    parser.add_argument(
        "--html", type=Path, default=DEFAULT_HTML, help="source web/index.html"
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=DEFAULT_OUT_DIR,
        help="directory that receives the generated JSONC files",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write anything; exit 1 if any file is out of date",
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

    try:
        text, newline = read_source(args.html)
        document = parse_document(text)
    except L10nError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.all_locales:
        locales = document.locales
    else:
        locales = list(dict.fromkeys(args.locales))

    errors: list[str] = []
    for locale in locales:
        if not LOCALE_RE.match(locale):
            errors.append(f"{locale}: not a locale such as ar-SA")
            continue
        if locale not in document.object_locales and locale not in document.row_locales:
            available = ", ".join(document.locales)
            errors.append(f"{locale}: unknown locale; available locales: {available}")
            continue

        try:
            english = english_reference(document)
        except L10nError as exc:
            errors.append(str(exc))
            break

        path = args.out_dir / f"{locale}.jsonc"
        content = render_catalog(
            document,
            locale,
            catalog_pairs(document, locale),
            english,
            "translations",
            f"python tools/merge_web_l10n.py {locale}",
        )
        if not emit(path, content, newline, args.check):
            errors.append(f"{locale}: {path.name} is out of date")

        if args.byte_units and (locale in document.byte_units or not args.all_locales):
            units = byte_unit_pairs(document.byte_units.get(locale))
            unit_path = args.out_dir / f"{locale}{BYTE_UNITS_SUFFIX}"
            try:
                unit_english = byte_unit_reference(document)
            except L10nError as exc:
                errors.append(str(exc))
                continue
            unit_content = render_catalog(
                document,
                locale,
                units,
                unit_english,
                "byte units",
                f"python tools/merge_web_l10n.py {locale} --byte-units",
            )
            if not emit(unit_path, unit_content, newline, args.check):
                errors.append(f"{locale}: {unit_path.name} is out of date")

    if errors:
        print("Extraction failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    verb = "checked" if args.check else "wrote"
    print(f"{verb} {len(locales)} locale(s) in {args.out_dir}")
    return 0


def emit(path: Path, content: str, newline: str, check: bool) -> bool:
    """Write ``content`` unless --check was given; report whether it is current."""
    if check:
        try:
            current, _ = read_source(path)
        except L10nError:
            return False
        return current == content
    write_text(path, content, newline)
    return True


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    raise SystemExit(main())
