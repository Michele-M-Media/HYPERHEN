#!/usr/bin/env python3
"""Generate the HyperDPI notification table from the plugin locale catalogs."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


PRINTF_CONVERSION = re.compile(
    r"%(?:\d+\$)?[-+ #0']*(?:\*|\d+)?(?:\.(?:\*|\d+))?"
    r"(?:hh|h|ll|l|j|z|t|L)?([diuoxXfFeEgGaAcspn%])"
)


def conversions(value: str) -> list[str]:
    return [item for item in PRINTF_CONVERSION.findall(value) if item != "%"]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--locales-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    locales = []
    for path in sorted(args.locales_dir.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        meta = data.get("meta", {})
        values = data.get("notifications")
        if not isinstance(meta.get("id"), str) or not isinstance(values, dict):
            raise SystemExit(f"error: invalid locale catalog: {path}")
        locales.append((path, meta, values))
    if not locales:
        raise SystemExit("error: no HyperDPI locale catalogs found")

    fallbacks = [item for item in locales if item[1].get("fallback")]
    if len(fallbacks) != 1:
        raise SystemExit("error: exactly one locale must be the fallback")
    fallback = fallbacks[0]
    locales = [fallback] + sorted(
        (item for item in locales if item is not fallback),
        key=lambda item: item[1]["id"],
    )

    expected_keys = list(fallback[2])
    for path, _, values in locales:
        if set(values) != set(expected_keys):
            raise SystemExit(f"error: notification key mismatch: {path}")
        for key in expected_keys:
            if conversions(values[key]) != conversions(fallback[2][key]):
                raise SystemExit(f"error: format mismatch for {key}: {path}")

    lines = [
        "/* Generated from the JSON locale catalogs. Do not edit. */",
        f"#define HYPERDPI_I18N_LOCALE_COUNT {len(locales)}",
        "static const char *const kHyperDpiLocaleIds[HYPERDPI_I18N_LOCALE_COUNT] = {",
        "    " + ", ".join(json.dumps(item[1]["id"]) for item in locales) + ",",
        "};",
        "typedef struct hyperdpi_translation {",
        "    const char *key;",
        "    const char *text[HYPERDPI_I18N_LOCALE_COUNT];",
        "} hyperdpi_translation;",
        "static const hyperdpi_translation kHyperDpiTranslations[] = {",
    ]
    for key in expected_keys:
        texts = ", ".join(
            json.dumps(item[2][key], ensure_ascii=False) for item in locales
        )
        lines.append(f"    {{{json.dumps(key)}, {{{texts}}}}},")
    lines.extend(["};", ""])

    output = "\n".join(lines)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != output:
        args.output.write_text(output, encoding="utf-8")


if __name__ == "__main__":
    main()
