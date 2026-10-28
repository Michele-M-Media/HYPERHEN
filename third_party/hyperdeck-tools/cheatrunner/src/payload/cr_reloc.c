#include <string.h>

#include "cr_reloc.h"
#include "hde64.h"

#define JMP_LEN  14
#define CALL_LEN 16
#define JCC_LEN  16
#define MAX_INSNS 32

static void
put_jmp(uint8_t *out, uint64_t target) {
  static const uint8_t op[6] = {0xff, 0x25, 0, 0, 0, 0};
  memcpy(out, op, sizeof(op));
  memcpy(out + 6, &target, 8);
}

/* call [rip+2]; jmp +8; dq target - keeps every general register intact. */
static void
put_call(uint8_t *out, uint64_t target) {
  static const uint8_t op[8] = {0xff, 0x15, 2, 0, 0, 0, 0xeb, 8};
  memcpy(out, op, sizeof(op));
  memcpy(out + 8, &target, 8);
}

static size_t
imm_size(const hde64s *h) {
  return ((h->flags & F_IMM8) ? 1 : 0) + ((h->flags & F_IMM16) ? 2 : 0) +
         ((h->flags & F_IMM32) ? 4 : 0) + ((h->flags & F_IMM64) ? 8 : 0);
}

bool
cr_relocate(const uint8_t *src, uint64_t src_addr, uint8_t *dst, uint64_t dst_addr,
            size_t min_len, size_t cap, size_t *src_len, const char **why) {
  uint64_t targets[MAX_INSNS];
  size_t ntargets = 0;
  size_t in = 0, out = 0;

  while (in < min_len) {
    hde64s h;
    if (ntargets >= MAX_INSNS) {
      *why = "too many relative operands";
      return false;
    }
    if (!hde64_disasm(src + in, &h) || (h.flags & F_ERROR)) {
      *why = "instruction decode error";
      return false;
    }
    uint64_t next = src_addr + in + h.len;

    /* VEX and EVEX prefixes are not decoded reliably, so those lengths can't be trusted. */
    if (h.opcode == 0xc4 || h.opcode == 0xc5 || h.opcode == 0x62) {
      *why = "VEX/EVEX instruction";
      return false;
    }

    if (h.flags & F_RELATIVE) {
      int64_t rel;
      if (h.flags & F_IMM8) {
        rel = (int8_t)h.imm.imm8;
      } else if (h.flags & F_IMM32) {
        rel = (int32_t)h.imm.imm32;
      } else {
        *why = "relative branch without immediate";
        return false;
      }
      uint64_t target = next + rel;
      size_t need;
      if (h.opcode == 0xe8) {
        need = CALL_LEN;
      } else if (h.opcode == 0xe9 || h.opcode == 0xeb) {
        need = JMP_LEN;
      } else if ((h.opcode >= 0x70 && h.opcode <= 0x7f) ||
                 (h.opcode == 0x0f && h.opcode2 >= 0x80 && h.opcode2 <= 0x8f)) {
        need = JCC_LEN;
      } else {
        *why = "unsupported relative opcode";
        return false;
      }
      if (out + need + JMP_LEN > cap) {
        *why = "trampoline too small";
        return false;
      }
      if (need == CALL_LEN) {
        put_call(dst + out, target);
      } else if (need == JMP_LEN) {
        put_jmp(dst + out, target);
      } else {
        uint8_t cc = (h.opcode == 0x0f ? h.opcode2 : h.opcode) & 0x0f;
        dst[out] = 0x70 | (cc ^ 1);
        dst[out + 1] = JMP_LEN;
        put_jmp(dst + out + 2, target);
      }
      targets[ntargets++] = target;
      out += need;
    } else {
      if (out + h.len + JMP_LEN > cap) {
        *why = "trampoline too small";
        return false;
      }
      memcpy(dst + out, src + in, h.len);
      if ((h.flags & F_MODRM) && h.modrm_mod == 0 && h.modrm_rm == 5 && !h.p_67) {
        if (!(h.flags & F_DISP32) || h.len < imm_size(&h) + 4) {
          *why = "bad rip-relative operand";
          return false;
        }
        size_t disp_off = h.len - imm_size(&h) - 4;
        int32_t disp;
        memcpy(&disp, src + in + disp_off, 4);
        uint64_t target = next + (int64_t)disp;
        int64_t moved = (int64_t)target - (int64_t)(dst_addr + out + h.len);
        if (moved < INT32_MIN || moved > INT32_MAX) {
          *why = "rip displacement out of range";
          return false;
        }
        int32_t fixed = (int32_t)moved;
        memcpy(dst + out + disp_off, &fixed, 4);
        targets[ntargets++] = target;
      }
      out += h.len;
    }
    in += h.len;
  }

  /* A target inside the stolen bytes would land mid-copy. */
  for (size_t i = 0; i < ntargets; i++) {
    if (targets[i] >= src_addr && targets[i] < src_addr + in) {
      *why = "branch into the stolen bytes";
      return false;
    }
  }

  put_jmp(dst + out, src_addr + in);
  *src_len = in;
  return true;
}
