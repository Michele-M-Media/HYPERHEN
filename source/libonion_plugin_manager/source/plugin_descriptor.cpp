#include <onion/plugin_manager.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace onion::plugin {
namespace {

constexpr size_t kElfHeaderSize = 64;
constexpr size_t kProgramHeaderSize = 56;
constexpr size_t kSectionHeaderSize = 64;
constexpr uint16_t kElfTypeExec = 2;
constexpr uint16_t kElfTypeDyn = 3;
constexpr uint16_t kMachineX86_64 = 62;
constexpr size_t kEtaHenPluginHeaderSize = 29;
constexpr size_t kMaxProgramHeaders = 128;
constexpr uint64_t kMaxPayloadSpan = 512ull * 1024ull * 1024ull;

template <typename T>
bool read_le(std::span<const uint8_t> image, size_t offset, T &out) {
  if (offset > image.size() || sizeof(T) > image.size() - offset) return false;
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(T); ++i)
    value |= static_cast<uint64_t>(image[offset + i]) << (i * 8);
  out = static_cast<T>(value);
  return true;
}

bool range_valid(size_t offset, size_t size, size_t total) {
  return offset <= total && size <= total - offset;
}

bool table_valid(uint64_t offset, uint16_t count, uint16_t entry_size,
                 size_t total) {
  if (offset > std::numeric_limits<size_t>::max()) return false;
  if (count != 0 && entry_size > std::numeric_limits<size_t>::max() / count)
    return false;
  return range_valid(static_cast<size_t>(offset),
                     static_cast<size_t>(count) * entry_size, total);
}

bool fixed_string(std::span<const uint8_t> field, std::string &out) {
  const auto end = std::find(field.begin(), field.end(), uint8_t{0});
  if (end == field.end()) return false;
  for (auto cursor = field.begin(); cursor != end; ++cursor) {
    if (*cursor < 0x20 || *cursor > 0x7e) return false;
  }
  out.assign(reinterpret_cast<const char *>(field.data()),
             static_cast<size_t>(end - field.begin()));
  return true;
}

bool valid_id(std::string_view value) {
  if (value.size() != 9) return false;
  for (size_t i = 0; i < 4; ++i) {
    const char c = value[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
  }
  for (size_t i = 4; i < value.size(); ++i)
    if (value[i] < '0' || value[i] > '9') return false;
  return true;
}

bool valid_version(std::string_view value) {
  const size_t dot = value.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 3 != value.size())
    return false;
  for (size_t i = 0; i < dot; ++i)
    if (value[i] < '0' || value[i] > '9') return false;
  return value[dot + 1] >= '0' && value[dot + 1] <= '9' &&
         value[dot + 2] >= '0' && value[dot + 2] <= '9';
}

Inspection fail(const char *message) { return {{}, message}; }

bool validate_standalone_elf(std::span<const uint8_t> image,
                             std::string &error) {
  if (image.size() < kElfHeaderSize) {
    error = "embedded ELF header is truncated";
    return false;
  }
  constexpr std::array<uint8_t, 7> magic{0x7f, 'E', 'L', 'F', 2, 1, 1};
  if (!std::equal(magic.begin(), magic.end(), image.begin())) {
    error = "embedded payload is not a little-endian ELF64";
    return false;
  }

  uint16_t type = 0, machine = 0, header_size = 0;
  uint16_t program_entry_size = 0, program_count = 0;
  uint32_t version = 0;
  uint64_t entry = 0, program_offset = 0;
  if (!read_le(image, 16, type) || !read_le(image, 18, machine) ||
      !read_le(image, 20, version) || !read_le(image, 24, entry) ||
      !read_le(image, 32, program_offset) ||
      !read_le(image, 52, header_size) ||
      !read_le(image, 54, program_entry_size) ||
      !read_le(image, 56, program_count)) {
    error = "embedded ELF header is truncated";
    return false;
  }
  if ((type != kElfTypeExec && type != kElfTypeDyn) ||
      machine != kMachineX86_64 || version != 1 ||
      header_size != kElfHeaderSize ||
      program_entry_size != kProgramHeaderSize || program_count == 0 ||
      program_count > kMaxProgramHeaders || program_offset < kElfHeaderSize ||
      !table_valid(program_offset, program_count, program_entry_size,
                   image.size())) {
    error = "embedded ELF layout is not supported";
    return false;
  }

  struct LoadRange {
    uint64_t begin;
    uint64_t end;
  };
  std::array<LoadRange, kMaxProgramHeaders> ranges{};
  size_t range_count = 0;
  uint64_t lowest = UINT64_MAX;
  uint64_t highest = 0;
  bool entry_is_executable = false;

  for (uint16_t index = 0; index < program_count; ++index) {
    const size_t header = static_cast<size_t>(program_offset) +
                          static_cast<size_t>(index) * program_entry_size;
    uint32_t kind = 0, flags = 0;
    uint64_t file_offset = 0, virtual_address = 0;
    uint64_t file_size = 0, memory_size = 0, alignment = 0;
    if (!read_le(image, header, kind) || !read_le(image, header + 4, flags) ||
        !read_le(image, header + 8, file_offset) ||
        !read_le(image, header + 16, virtual_address) ||
        !read_le(image, header + 32, file_size) ||
        !read_le(image, header + 40, memory_size) ||
        !read_le(image, header + 48, alignment) ||
        file_offset > std::numeric_limits<size_t>::max() ||
        file_size > std::numeric_limits<size_t>::max() ||
        !range_valid(static_cast<size_t>(file_offset),
                     static_cast<size_t>(file_size), image.size())) {
      error = "embedded ELF segment is outside the file";
      return false;
    }
    if (kind == 3) {
      error = "embedded ELF interpreter is not supported";
      return false;
    }
    if (kind != 1 || memory_size == 0)
      continue;
    if (file_size > memory_size || virtual_address > 0x7fffffffffffULL ||
        memory_size > 0x800000000000ULL - virtual_address ||
        (alignment > 1 &&
         ((alignment & (alignment - 1)) != 0 ||
          virtual_address % alignment != file_offset % alignment))) {
      error = "embedded ELF load segment is invalid";
      return false;
    }
    const uint64_t end = virtual_address + memory_size;
    for (size_t prior = 0; prior < range_count; ++prior) {
      if (virtual_address < ranges[prior].end &&
          ranges[prior].begin < end) {
        error = "embedded ELF load segments overlap";
        return false;
      }
    }
    ranges[range_count++] = {virtual_address, end};
    lowest = std::min(lowest, virtual_address);
    highest = std::max(highest, end);
    if ((flags & 1u) != 0 && entry >= virtual_address &&
        entry - virtual_address < file_size)
      entry_is_executable = true;
  }

  if (!entry_is_executable || lowest == UINT64_MAX || highest < lowest ||
      highest - lowest > kMaxPayloadSpan) {
    error = "embedded ELF has no valid executable entry point";
    return false;
  }
  return true;
}

} // namespace

Inspection inspect_elf(std::span<const uint8_t> image) {
  if (image.size() < kElfHeaderSize) return fail("ELF header is truncated");
  constexpr std::array<uint8_t, 4> magic{0x7f, 'E', 'L', 'F'};
  if (!std::equal(magic.begin(), magic.end(), image.begin()) || image[4] != 2 ||
      image[5] != 1 || image[6] != 1)
    return fail("expected a little-endian ELF64");

  uint16_t type = 0, machine = 0, section_entry_size = 0, section_count = 0;
  uint16_t string_section_index = 0, program_entry_size = 0;
  uint16_t program_count = 0;
  uint64_t program_offset = 0, section_offset = 0;
  if (!read_le(image, 16, type) || !read_le(image, 18, machine) ||
      !read_le(image, 32, program_offset) ||
      !read_le(image, 40, section_offset) ||
      !read_le(image, 54, program_entry_size) ||
      !read_le(image, 56, program_count) ||
      !read_le(image, 58, section_entry_size) ||
      !read_le(image, 60, section_count) ||
      !read_le(image, 62, string_section_index))
    return fail("ELF header is truncated");
  if ((type != kElfTypeExec && type != kElfTypeDyn) || machine != kMachineX86_64)
    return fail("ELF type or machine is not supported by the private loader");
  if (program_entry_size != 56 ||
      !table_valid(program_offset, program_count, program_entry_size,
                   image.size()) ||
      section_entry_size != kSectionHeaderSize ||
      section_count == 0 || string_section_index >= section_count ||
      !table_valid(section_offset, section_count, section_entry_size,
                   image.size()))
    return fail("ELF has no valid section table");

  const size_t section_table = static_cast<size_t>(section_offset);
  const size_t strings_header =
      section_table + static_cast<size_t>(string_section_index) * section_entry_size;
  uint64_t strings_offset64 = 0, strings_size64 = 0;
  if (!read_le(image, strings_header + 24, strings_offset64) ||
      !read_le(image, strings_header + 32, strings_size64) ||
      strings_offset64 > std::numeric_limits<size_t>::max() ||
      strings_size64 > std::numeric_limits<size_t>::max())
    return fail("section string table is invalid");
  const size_t strings_offset = static_cast<size_t>(strings_offset64);
  const size_t strings_size = static_cast<size_t>(strings_size64);
  if (!range_valid(strings_offset, strings_size, image.size()))
    return fail("section string table is outside the ELF");
  const auto strings = image.subspan(strings_offset, strings_size);

  for (uint16_t index = 0; index < section_count; ++index) {
    const size_t header = section_table + static_cast<size_t>(index) * section_entry_size;
    uint32_t name_offset = 0;
    uint64_t data_offset64 = 0, data_size64 = 0;
    if (!read_le(image, header, name_offset) ||
        !read_le(image, header + 24, data_offset64) ||
        !read_le(image, header + 32, data_size64) || name_offset >= strings.size())
      return fail("section header is invalid");
    const void *terminator = std::memchr(strings.data() + name_offset, 0,
                                         strings.size() - name_offset);
    if (!terminator) return fail("section name is not NUL-terminated");
    const auto *name_end = static_cast<const uint8_t *>(terminator);
    const std::string_view name(
        reinterpret_cast<const char *>(strings.data() + name_offset),
        static_cast<size_t>(name_end - (strings.data() + name_offset)));
    if (name != ".onion_plugin") continue;

    if (data_offset64 > std::numeric_limits<size_t>::max() ||
        data_size64 > std::numeric_limits<size_t>::max())
      return fail(".onion_plugin section is outside the ELF");
    const size_t data_offset = static_cast<size_t>(data_offset64);
    const size_t data_size = static_cast<size_t>(data_size64);
    if (data_size < kDescriptorV1Size ||
        !range_valid(data_offset, data_size, image.size()))
      return fail(".onion_plugin descriptor is truncated");
    const auto raw = image.subspan(data_offset, data_size);

    Descriptor descriptor;
    if (!read_le(raw, 0, descriptor.struct_size) ||
        !read_le(raw, 4, descriptor.abi_version) ||
        !read_le(raw, 8, descriptor.capabilities) ||
        !read_le(raw, 12, descriptor.flags))
      return fail(".onion_plugin descriptor is truncated");
    if (descriptor.struct_size < kDescriptorV1Size ||
        descriptor.struct_size > data_size)
      return fail("plugin descriptor struct_size is invalid");
    if (descriptor.abi_version != kAbiVersion)
      return fail("plugin ABI is not supported");
    if ((descriptor.capabilities & ~kKnownCapabilities) != 0)
      return fail("plugin descriptor contains unknown capabilities");
    if ((descriptor.flags & ~kKnownFlags) != 0)
      return fail("plugin descriptor contains unknown flags");
    if (!fixed_string(raw.subspan(16, kIdCapacity), descriptor.plugin_id) ||
        !fixed_string(raw.subspan(48, kVersionCapacity), descriptor.version) ||
        !fixed_string(raw.subspan(64, kNameCapacity), descriptor.name))
      return fail("plugin descriptor strings must be printable ASCII and NUL-terminated");
    if (!valid_id(descriptor.plugin_id)) return fail("plugin ID is invalid");
    if (!valid_version(descriptor.version)) return fail("plugin version is invalid");
    if (descriptor.name.empty()) return fail("plugin name is empty");
    return {std::move(descriptor), {}};
  }

  return fail("ELF has no .onion_plugin section");
}

Inspection inspect_etahen_plugin(std::span<const uint8_t> image,
                                 size_t *elf_offset) {
  /* GPLv3 interoperability path adapted from TacHen 0.1.0-beta.3's
   * etaHEN_PLUGIN container validator.  The 29-byte wrapper carries a plugin
   * identity and version; only its embedded, independently validated ELF is
   * passed to HyperHEN's private loader. */
  constexpr std::array<uint8_t, 14> magic{
      'e', 't', 'a', 'H', 'E', 'N', '_', 'P', 'L', 'U', 'G', 'I', 'N', 0};
  if (image.size() < kEtaHenPluginHeaderSize + kElfHeaderSize ||
      !std::equal(magic.begin(), magic.end(), image.begin()) ||
      image[23] != 0 || image[28] != 0)
    return fail("invalid etaHEN .plugin container header");

  std::string plugin_id(reinterpret_cast<const char *>(image.data() + 14), 9);
  std::string version(reinterpret_cast<const char *>(image.data() + 24), 4);
  if (!valid_id(plugin_id) || !valid_version(version))
    return fail("invalid etaHEN plugin identity or version");

  std::string validation_error;
  if (!validate_standalone_elf(image.subspan(kEtaHenPluginHeaderSize),
                               validation_error))
    return {{}, std::move(validation_error)};

  Descriptor descriptor;
  descriptor.struct_size = kDescriptorV1Size;
  descriptor.abi_version = kAbiVersion;
  descriptor.capabilities = 0;
  descriptor.flags = kFlagLongRunning;
  descriptor.plugin_id = std::move(plugin_id);
  descriptor.version = std::move(version);
  descriptor.name = descriptor.plugin_id;
  if (elf_offset)
    *elf_offset = kEtaHenPluginHeaderSize;
  return {std::move(descriptor), {}};
}

} // namespace onion::plugin
