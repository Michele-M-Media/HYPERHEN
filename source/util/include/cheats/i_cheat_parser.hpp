#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "cheats/cheat_engine.h"

namespace onion::cheats {

/**
 * Strategy: parse a single cheat-file format into onion_cheat_file_t.
 *
 * Implementations: JSON, SHN(XML), MC4 (decrypt → XML), ShnExt (C adapter).
 */
class ICheatParser {
public:
  virtual ~ICheatParser() = default;

  /**
   * Parse raw bytes into out. Clears out first.
   * @return 0 on success (at least one cheat), negative on failure.
   */
  virtual int parse(const uint8_t *data, size_t size,
                    onion_cheat_file_t &out) = 0;

  /** Short format id: "json", "shn", "mc4", "ShnExt". */
  virtual const char *name() const = 0;
};

/**
 * Factory / registry for format loaders.
 * Patterns: Factory Method + Strategy selection by extension.
 */
class CheatParserFactory {
public:
  /**
   * Create parser for a format name (case-insensitive for ShnExt).
   * Accepts "json", "shn", "mc4", "shnext" / "ShnExt", optionally with leading '.'.
   */
  static std::unique_ptr<ICheatParser>
  createByFormat(const std::string &format);

  /** Create parser from file path extension (default: json). */
  static std::unique_ptr<ICheatParser>
  createByPath(const std::string &path);

  static int loadFile(const std::string &path, onion_cheat_file_t &out);
  static int loadBuffer(const std::string &format, const uint8_t *data,
                        size_t size, onion_cheat_file_t &out);
};

} // namespace onion::cheats
