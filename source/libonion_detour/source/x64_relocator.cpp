/* Copyright (C) 2026 OnionHEN */

#include <onion/x64_relocator.h>

#include <onion/hde64.h>

#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

constexpr size_t kAbsoluteJumpSize = 14;
constexpr size_t kAbsoluteCallSize = 16;
constexpr size_t kConditionalJumpSize = 16;
constexpr size_t kMaxInstructions = 32;

enum class BranchKind : uint8_t {
  None,
  Call,
  Jump,
  ConditionalJump,
};

struct RelocatedInstruction {
  hde64s decoded{};
  size_t source_offset = 0;
  size_t destination_offset = 0;
  size_t destination_size = 0;
  BranchKind branch = BranchKind::None;
  uint8_t condition = 0;
};

void set_error(onion_x64_relocate_result *result,
               onion_x64_relocate_error error, size_t offset) {
  result->error = error;
  result->error_offset = offset;
}

size_t immediate_size(const hde64s &decoded) {
  size_t size = 0;
  if (decoded.flags & F_IMM8)
    size += 1;
  if (decoded.flags & F_IMM16)
    size += 2;
  if (decoded.flags & F_IMM32)
    size += 4;
  if (decoded.flags & F_IMM64)
    size += 8;
  return size;
}

bool is_rip_relative_memory(const hde64s &decoded) {
  return (decoded.flags & F_MODRM) && decoded.modrm_mod == 0 &&
         decoded.modrm_rm == 5 && decoded.p_67 == 0;
}

bool add_signed(uintptr_t base, int64_t displacement, uintptr_t *target) {
  if (displacement >= 0) {
    const uintptr_t amount = static_cast<uintptr_t>(displacement);
    if (base > std::numeric_limits<uintptr_t>::max() - amount)
      return false;
    *target = base + amount;
    return true;
  }

  /* Avoid negating INT64_MIN. Relative x86 displacements are at most int32. */
  const uintptr_t amount = static_cast<uintptr_t>(-displacement);
  if (base < amount)
    return false;
  *target = base - amount;
  return true;
}

bool relative_target(const RelocatedInstruction &instruction,
                     uintptr_t source_address, uintptr_t *target) {
  int64_t displacement = 0;
  if (instruction.decoded.flags & F_IMM8) {
    displacement = static_cast<int8_t>(instruction.decoded.imm.imm8);
  } else if (instruction.decoded.flags & F_IMM32) {
    displacement = static_cast<int32_t>(instruction.decoded.imm.imm32);
  } else {
    return false;
  }

  const uintptr_t next = source_address + instruction.source_offset +
                         instruction.decoded.len;
  return add_signed(next, displacement, target);
}

bool classify_relative(RelocatedInstruction &instruction) {
  const hde64s &decoded = instruction.decoded;
  if (!(decoded.flags & F_RELATIVE))
    return true;

  if (decoded.opcode == 0xe8 && (decoded.flags & F_IMM32)) {
    instruction.branch = BranchKind::Call;
    instruction.destination_size = kAbsoluteCallSize;
    return true;
  }
  if ((decoded.opcode == 0xe9 && (decoded.flags & F_IMM32)) ||
      (decoded.opcode == 0xeb && (decoded.flags & F_IMM8))) {
    instruction.branch = BranchKind::Jump;
    instruction.destination_size = kAbsoluteJumpSize;
    return true;
  }
  if (decoded.opcode >= 0x70 && decoded.opcode <= 0x7f &&
      (decoded.flags & F_IMM8)) {
    instruction.branch = BranchKind::ConditionalJump;
    instruction.condition = decoded.opcode & 0x0f;
    instruction.destination_size = kConditionalJumpSize;
    return true;
  }
  if (decoded.opcode == 0x0f && decoded.opcode2 >= 0x80 &&
      decoded.opcode2 <= 0x8f && (decoded.flags & F_IMM32)) {
    instruction.branch = BranchKind::ConditionalJump;
    instruction.condition = decoded.opcode2 & 0x0f;
    instruction.destination_size = kConditionalJumpSize;
    return true;
  }
  return false;
}

bool write_absolute_jump(uint8_t *destination, size_t capacity,
                         uintptr_t target) {
  if (capacity < kAbsoluteJumpSize)
    return false;
  static constexpr uint8_t prefix[] = {0xff, 0x25, 0, 0, 0, 0};
  std::memcpy(destination, prefix, sizeof(prefix));
  const uint64_t address = target;
  std::memcpy(destination + sizeof(prefix), &address, sizeof(address));
  return true;
}

bool write_absolute_call(uint8_t *destination, size_t capacity,
                         uintptr_t target) {
  if (capacity < kAbsoluteCallSize)
    return false;
  /* call [rip+2]; jmp +8; dq target — preserves every general register. */
  static constexpr uint8_t prefix[] = {0xff, 0x15, 0x02, 0x00,
                                       0x00, 0x00, 0xeb, 0x08};
  std::memcpy(destination, prefix, sizeof(prefix));
  const uint64_t address = target;
  std::memcpy(destination + sizeof(prefix), &address, sizeof(address));
  return true;
}

bool map_internal_target(uintptr_t target, uintptr_t source_address,
                         size_t source_size, uintptr_t destination_address,
                         const RelocatedInstruction *instructions,
                         size_t instruction_count, uintptr_t *mapped) {
  if (target < source_address || target >= source_address + source_size) {
    *mapped = target;
    return true;
  }

  const size_t target_offset = static_cast<size_t>(target - source_address);
  for (size_t i = 0; i < instruction_count; ++i) {
    if (instructions[i].source_offset == target_offset) {
      *mapped = destination_address + instructions[i].destination_offset;
      return true;
    }
  }
  return false;
}

bool relocate_rip_memory(const RelocatedInstruction &instruction,
                         uintptr_t source_address, uintptr_t destination_address,
                         size_t source_size,
                         const RelocatedInstruction *instructions,
                         size_t instruction_count, uint8_t *destination,
                         onion_x64_relocate_result *result) {
  const size_t immediate = immediate_size(instruction.decoded);
  if (instruction.decoded.len < immediate + sizeof(int32_t)) {
    set_error(result, ONION_X64_RELOCATE_DECODE_ERROR,
              instruction.source_offset);
    return false;
  }
  const size_t displacement_offset =
      instruction.decoded.len - immediate - sizeof(int32_t);
  int32_t old_displacement = 0;
  std::memcpy(&old_displacement,
              destination + instruction.destination_offset +
                  displacement_offset,
              sizeof(old_displacement));

  const uintptr_t source_next = source_address + instruction.source_offset +
                                instruction.decoded.len;
  uintptr_t target = 0;
  if (!add_signed(source_next, old_displacement, &target)) {
    set_error(result, ONION_X64_RELOCATE_DECODE_ERROR,
              instruction.source_offset);
    return false;
  }
  if (!map_internal_target(target, source_address, source_size,
                           destination_address, instructions,
                           instruction_count, &target)) {
    set_error(result, ONION_X64_RELOCATE_INTERNAL_TARGET_NOT_BOUNDARY,
              instruction.source_offset);
    return false;
  }

  const uintptr_t destination_next =
      destination_address + instruction.destination_offset +
      instruction.decoded.len;
  const int64_t new_displacement =
      static_cast<int64_t>(target) - static_cast<int64_t>(destination_next);
  if (new_displacement < INT32_MIN || new_displacement > INT32_MAX) {
    set_error(result, ONION_X64_RELOCATE_RIP_DISPLACEMENT_OUT_OF_RANGE,
              instruction.source_offset);
    return false;
  }
  const int32_t encoded = static_cast<int32_t>(new_displacement);
  std::memcpy(destination + instruction.destination_offset +
                  displacement_offset,
              &encoded, sizeof(encoded));
  return true;
}

} // namespace

extern "C" bool onion_x64_relocate(
    const uint8_t *source, uintptr_t source_address, uint8_t *destination,
    uintptr_t destination_address, size_t min_source_size,
    size_t destination_capacity, onion_x64_relocate_result *result) {
  if (!result)
    return false;
  *result = {};
  if (!source || !destination || source_address == 0 ||
      destination_address == 0 || min_source_size == 0) {
    set_error(result, ONION_X64_RELOCATE_INVALID_ARGUMENT, 0);
    return false;
  }

  RelocatedInstruction instructions[kMaxInstructions]{};
  size_t instruction_count = 0;
  size_t source_size = 0;
  size_t destination_size = 0;

  while (source_size < min_source_size) {
    if (instruction_count >= kMaxInstructions) {
      set_error(result, ONION_X64_RELOCATE_TOO_MANY_INSTRUCTIONS, source_size);
      return false;
    }
    RelocatedInstruction &instruction = instructions[instruction_count];
    instruction.source_offset = source_size;
    instruction.destination_offset = destination_size;
    const unsigned decoded_size =
        hde64_disasm(source + source_size, &instruction.decoded);
    if (decoded_size == 0 || (instruction.decoded.flags & F_ERROR)) {
      set_error(result, ONION_X64_RELOCATE_DECODE_ERROR, source_size);
      return false;
    }
    instruction.destination_size = instruction.decoded.len;
    if (!classify_relative(instruction)) {
      set_error(result, ONION_X64_RELOCATE_UNSUPPORTED_RELATIVE, source_size);
      return false;
    }
    source_size += instruction.decoded.len;
    destination_size += instruction.destination_size;
    ++instruction_count;
  }

  result->source_size = source_size;
  if (destination_size > destination_capacity ||
      kAbsoluteJumpSize > destination_capacity - destination_size) {
    set_error(result, ONION_X64_RELOCATE_OUTPUT_TOO_SMALL, source_size);
    return false;
  }

  for (size_t i = 0; i < instruction_count; ++i) {
    const RelocatedInstruction &instruction = instructions[i];
    uint8_t *output = destination + instruction.destination_offset;
    if (instruction.branch == BranchKind::None) {
      std::memcpy(output, source + instruction.source_offset,
                  instruction.decoded.len);
      if (is_rip_relative_memory(instruction.decoded) &&
          !relocate_rip_memory(instruction, source_address,
                               destination_address, source_size, instructions,
                               instruction_count, destination, result)) {
        return false;
      }
      continue;
    }

    uintptr_t target = 0;
    if (!relative_target(instruction, source_address, &target)) {
      set_error(result, ONION_X64_RELOCATE_UNSUPPORTED_RELATIVE,
                instruction.source_offset);
      return false;
    }
    if (!map_internal_target(target, source_address, source_size,
                             destination_address, instructions,
                             instruction_count, &target)) {
      set_error(result, ONION_X64_RELOCATE_INTERNAL_TARGET_NOT_BOUNDARY,
                instruction.source_offset);
      return false;
    }

    if (instruction.branch == BranchKind::Call) {
      (void)write_absolute_call(output, instruction.destination_size, target);
    } else if (instruction.branch == BranchKind::Jump) {
      (void)write_absolute_jump(output, instruction.destination_size, target);
    } else {
      /* Invert the condition and skip the following 14-byte absolute jump. */
      output[0] = static_cast<uint8_t>(0x70 | (instruction.condition ^ 1));
      output[1] = static_cast<uint8_t>(kAbsoluteJumpSize);
      (void)write_absolute_jump(output + 2, instruction.destination_size - 2,
                                target);
    }
  }

  (void)write_absolute_jump(destination + destination_size,
                            destination_capacity - destination_size,
                            source_address + source_size);
  result->trampoline_size = destination_size + kAbsoluteJumpSize;
  result->error = ONION_X64_RELOCATE_OK;
  result->error_offset = 0;
  return true;
}

extern "C" const char *onion_x64_relocate_error_string(
    onion_x64_relocate_error error) {
  switch (error) {
  case ONION_X64_RELOCATE_OK:
    return "ok";
  case ONION_X64_RELOCATE_INVALID_ARGUMENT:
    return "invalid argument";
  case ONION_X64_RELOCATE_DECODE_ERROR:
    return "instruction decode error";
  case ONION_X64_RELOCATE_TOO_MANY_INSTRUCTIONS:
    return "too many instructions";
  case ONION_X64_RELOCATE_OUTPUT_TOO_SMALL:
    return "trampoline output too small";
  case ONION_X64_RELOCATE_UNSUPPORTED_RELATIVE:
    return "unsupported relative instruction";
  case ONION_X64_RELOCATE_INTERNAL_TARGET_NOT_BOUNDARY:
    return "internal target is not an instruction boundary";
  case ONION_X64_RELOCATE_RIP_DISPLACEMENT_OUT_OF_RANGE:
    return "RIP-relative displacement is out of range";
  }
  return "unknown relocation error";
}
