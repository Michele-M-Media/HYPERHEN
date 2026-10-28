#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/cJSON.h"
#include "third_party/mc4/mc4decrypter.h"
#include "cr_paths.h"
#include "cr_log.h"
#include "cr_cheat_formats.h"

int
recognised_cheat_extension(const char *name) {
  size_t n = strlen(name);
  if (n > 5 && !strcasecmp(name + n - 5, ".json")) {
    return 1;
  }
  if (n > 4 && !strcasecmp(name + n - 4, ".shn")) {
    return 2;
  }
  if (n > 4 && !strcasecmp(name + n - 4, ".mc4")) {
    return 3;
  }
  return 0;
}

typedef struct cheat_buf {
  char *buf;
  size_t len;
  size_t cap;
} cheat_buf_t;

static void
cheat_buf_putc(cheat_buf_t *b, char c) {
  if (!b) {
    return;
  }
  if (b->len + 2 > b->cap) {
    size_t next = b->cap ? (b->cap * 2) : 1024;
    char *nb = realloc(b->buf, next);
    if (!nb) {
      return;
    }
    b->buf = nb;
    b->cap = next;
  }
  b->buf[b->len++] = c;
  b->buf[b->len] = '\0';
}

static void
cheat_buf_puts(cheat_buf_t *b, const char *s) {
  if (!b || !s) {
    return;
  }
  while (*s) {
    cheat_buf_putc(b, *s++);
  }
}

static void
cheat_buf_puts_json(cheat_buf_t *b, const char *s, size_t n) {
  if (!b || !s) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    switch (c) {
    case '"':
      cheat_buf_putc(b, '\\');
      cheat_buf_putc(b, '"');
      break;
    case '\\':
      cheat_buf_putc(b, '\\');
      cheat_buf_putc(b, '\\');
      break;
    case '\n':
      cheat_buf_putc(b, '\\');
      cheat_buf_putc(b, 'n');
      break;
    case '\r':
      cheat_buf_putc(b, '\\');
      cheat_buf_putc(b, 'r');
      break;
    case '\t':
      cheat_buf_putc(b, '\\');
      cheat_buf_putc(b, 't');
      break;
    default:
      if ((unsigned char)c < 0x20) {
        char esc[8];
        snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)c);
        cheat_buf_puts(b, esc);
      } else {
        cheat_buf_putc(b, c);
      }
      break;
    }
  }
}

const char *
xml_find_attr(const char *node, const char *attr, size_t *len_out) {
  char needle[64];
  const char *p;
  char quote;
  if (!node || !attr || !len_out) {
    return NULL;
  }
  snprintf(needle, sizeof(needle), "%s=\"", attr);
  p = strstr(node, needle);
  if (p) {
    quote = '"';
  } else {
    snprintf(needle, sizeof(needle), "%s='", attr);
    p = strstr(node, needle);
    if (!p) {
      return NULL;
    }
    quote = '\'';
  }
  p += strlen(needle);
  const char *end = strchr(p, quote);
  if (!end) {
    return NULL;
  }
  *len_out = (size_t)(end - p);
  return p;
}

static const char *
xml_find_child(const char *node, const char *tag, size_t *len_out) {
  char open[64];
  char close[64];
  const char *p;
  const char *end;
  if (!node || !tag || !len_out) {
    return NULL;
  }
  snprintf(open, sizeof(open), "<%s>", tag);
  snprintf(close, sizeof(close), "</%s>", tag);
  p = strstr(node, open);
  if (!p) {
    return NULL;
  }
  p += strlen(open);
  end = strstr(p, close);
  if (!end) {
    return NULL;
  }
  *len_out = (size_t)(end - p);
  return p;
}

char *
mc4_decrypt_to_xml(const char *cipher, size_t cipher_len, size_t *xml_size_out) {
  if (!cipher || cipher_len == 0) {
    return NULL;
  }

  uint8_t *scratch = malloc(cipher_len + 1);
  if (!scratch) {
    return NULL;
  }
  memcpy(scratch, cipher, cipher_len);
  scratch[cipher_len] = '\0';

  size_t plain_size = cipher_len;
  uint8_t *plain = decrypt_data(scratch, &plain_size);
  if (!plain) {
    free(scratch);
    return NULL;
  }
  if (plain == scratch) {
    free(scratch);
    return NULL;
  }
  free(scratch);

  char *xml = malloc(plain_size + 1);
  if (!xml) {
    free(plain);
    return NULL;
  }
  memcpy(xml, plain, plain_size);
  xml[plain_size] = '\0';
  free(plain);

  static const struct {
    const char *from;
    char to;
  } repl[] = {
      {"&lt;",    '<'},
      {"&gt;",    '>'},
      {"\\&quot;",'"'},
      {"&quot;",  '"'},
      {"&apos;",  '\''},
      {"&amp;",   '&'},   /* must be last — avoids re-expanding already-decoded & */
  };

  size_t out_len = strlen(xml);
  for (size_t r = 0; r < sizeof(repl) / sizeof(repl[0]); r++) {
    const char *from = repl[r].from;
    size_t flen = strlen(from);
    char *src = xml;
    char *dst = xml;
    while (*src) {
      if (strncmp(src, from, flen) == 0) {
        *dst++ = repl[r].to;
        src += flen;
      } else {
        *dst++ = *src++;
      }
    }
    *dst = '\0';
    out_len = (size_t)(dst - xml);
  }
  /* Wrong-key/corrupted decrypt produces non-NULL garbage; valid XML always starts with '<'. */
  if (out_len == 0 || xml[0] != '<') {
    free(xml);
    return NULL;
  }
  if (xml_size_out) {
    *xml_size_out = out_len;
  }
  return xml;
}

char *
shn_xml_to_json(const char *xml, size_t xml_len) {
  (void)xml_len;
  if (!xml) {
    return NULL;
  }
  cheat_buf_t out = {0};
  cheat_buf_puts(&out, "{\"name\":\"\",\"id\":\"\",\"version\":\"\",\"process\":\"eboot.bin\",\"mods\":[");

  char moder[256] = {0};
  size_t alen = 0;
  const char *trainer = strstr(xml, "<Trainer");
  if (trainer) {
    const char *aend = strchr(trainer, '>');
    if (aend) {
      char tag[1024];
      size_t tn = (size_t)(aend - trainer);
      if (tn < sizeof(tag)) {
        memcpy(tag, trainer, tn);
        tag[tn] = '\0';
        {
          size_t moder_len = 0;
          const char *mv = xml_find_attr(tag, "Moder", &moder_len);
          if (mv && moder_len > 0 && moder_len < sizeof(moder)) {
            memcpy(moder, mv, moder_len);
            moder[moder_len] = '\0';
          }
        }
        const char *v = xml_find_attr(tag, "Game", &alen);
        if (!v) {
          v = xml_find_attr(tag, "GameName", &alen);
        }
        if (v && alen > 0 && out.buf) {
          char *needle = strstr(out.buf, "\"name\":\"\"");
          if (needle) {
            cheat_buf_t tmp = {0};
            cheat_buf_t merged = {0};
            size_t prefix = (size_t)(needle - out.buf);
            cheat_buf_puts(&tmp, "\"name\":\"");
            cheat_buf_puts_json(&tmp, v, alen);
            cheat_buf_puts(&tmp, "\"");
            for (size_t i = 0; i < prefix; i++) {
              cheat_buf_putc(&merged, out.buf[i]);
            }
            if (tmp.buf) {
              cheat_buf_puts(&merged, tmp.buf);
            }
            cheat_buf_puts(&merged, out.buf + prefix + 9);
            free(tmp.buf);
            free(out.buf);
            out = merged;
          }
        }
      }
    }
  }

  int first = 1;
  const char *cur = xml;
  while ((cur = strstr(cur, "<Cheat ")) != NULL) {
    const char *close = strchr(cur, '>');
    if (!close) {
      break;
    }
    size_t hdr_len = (size_t)(close - cur);
    char hdr[2048];
    if (hdr_len >= sizeof(hdr)) {
      cr_log("warn", "cheat_fmt", "skipping <Cheat> element: header too large (%zu bytes, limit %zu) — cheat will be missing from mod list",
             hdr_len, sizeof(hdr) - 1);
      cur = close;
      continue;
    }
    memcpy(hdr, cur, hdr_len);
    hdr[hdr_len] = '\0';

    const char *body_end = strstr(close, "</Cheat>");
    if (!body_end) {
      break;
    }
    const char *body_start = close + 1;

    if (!first) {
      cheat_buf_putc(&out, ',');
    }
    first = 0;

    cheat_buf_puts(&out, "{");
    cheat_buf_puts(&out, "\"name\":\"");
    const char *t = xml_find_attr(hdr, "Text", &alen);
    if (!t) {
      t = xml_find_attr(hdr, "CheatName", &alen);
    }
    if (!t) {
      t = xml_find_attr(hdr, "Name", &alen);
    }
    if (t) {
      cheat_buf_puts_json(&out, t, alen);
    }
    cheat_buf_puts(&out, "\",");

    cheat_buf_puts(&out, "\"hint\":");
    t = xml_find_attr(hdr, "Description", &alen);
    if (t && alen > 0) {
      cheat_buf_puts(&out, "\"");
      cheat_buf_puts_json(&out, t, alen);
      cheat_buf_puts(&out, "\"");
    } else {
      cheat_buf_puts(&out, "null");
    }
    cheat_buf_puts(&out, ",");

    {
      size_t type_alen = 0;
      const char *type_v = xml_find_attr(hdr, "Type", &type_alen);
      int is_button = (type_v && type_alen >= 6 &&
                       !strncasecmp(type_v, "button", 6));
      cheat_buf_puts(&out, is_button ? "\"type\":\"button\"," : "\"type\":\"checkbox\",");
    }
    cheat_buf_puts(&out, "\"memory\":[");

    int first_mem = 1;
    const char *line_cur = body_start;
    while (line_cur < body_end && (line_cur = strstr(line_cur, "<Cheatline")) != NULL && line_cur < body_end) {
      const char *line_close = strstr(line_cur, "</Cheatline>");
      const char *line_self = strstr(line_cur, "/>");
      const char *line_end = NULL;
      if (line_close && (!line_self || line_close < line_self)) {
        line_end = line_close + strlen("</Cheatline>");
      } else if (line_self) {
        line_end = line_self + 2;
      } else {
        break;
      }

      char chunk[4096];
      size_t cl = (size_t)(line_end - line_cur);
      if (cl >= sizeof(chunk)) {
        cr_log("warn", "cheat_fmt", "skipping <Cheatline> element: content too large (%zu bytes, limit %zu) — one memory entry will be missing",
               cl, sizeof(chunk) - 1);
        line_cur = line_end;
        continue;
      }
      memcpy(chunk, line_cur, cl);
      chunk[cl] = '\0';

      size_t off_l = 0;
      size_t on_l = 0;
      size_t off2_l = 0;
      size_t abs_l = 0;
      size_t exp_l = 0;
      size_t mod_l = 0;
      const char *off  = xml_find_child(chunk, "Offset",     &off_l);
      const char *on   = xml_find_child(chunk, "ValueOn",    &on_l);
      const char *off2 = xml_find_child(chunk, "ValueOff",   &off2_l);
      const char *abs_ = xml_find_child(chunk, "Absolute",   &abs_l);
      const char *exp_ = xml_find_child(chunk, "Expected",   &exp_l);
      const char *mod_ = xml_find_child(chunk, "ModuleName", &mod_l);
      size_t sec_l = 0;
      const char *sec_ = xml_find_child(chunk, "Section", &sec_l);
      int section_num = 0;
      if (sec_ && sec_l > 0 && sec_l < 8) {
        char sec_buf[8] = {0};
        memcpy(sec_buf, sec_, sec_l);
        section_num = atoi(sec_buf);
      }
      if (off && on && off2) {
        if (!first_mem) {
          cheat_buf_putc(&out, ',');
        }
        first_mem = 0;
        cheat_buf_puts(&out, "{\"offset\":\"");
        cheat_buf_puts_json(&out, off, off_l);
        cheat_buf_puts(&out, "\",\"on\":\"");
        cheat_buf_puts_json(&out, on, on_l);
        cheat_buf_puts(&out, "\",\"off\":\"");
        cheat_buf_puts_json(&out, off2, off2_l);
        cheat_buf_puts(&out, "\"");
        if (exp_ && exp_l > 0) {
          cheat_buf_puts(&out, ",\"expected\":\"");
          cheat_buf_puts_json(&out, exp_, exp_l);
          cheat_buf_puts(&out, "\"");
        }
        if (mod_ && mod_l > 0) {
          cheat_buf_puts(&out, ",\"module_name\":\"");
          cheat_buf_puts_json(&out, mod_, mod_l);
          cheat_buf_puts(&out, "\"");
        }
        if (abs_ && abs_l > 0 &&
            (!strncasecmp(abs_, "true", 4) || abs_[0] == '1')) {
          cheat_buf_puts(&out, ",\"absolute\":true");
        }
        if (section_num != 0) {
          char sec_s[32];
          snprintf(sec_s, sizeof(sec_s), ",\"section\":%d", section_num);
          cheat_buf_puts(&out, sec_s);
        }
        cheat_buf_puts(&out, "}");
      }
      line_cur = line_end;
    }
    cheat_buf_puts(&out, "]}");
    cur = body_end + strlen("</Cheat>");
  }

  cheat_buf_puts(&out, "]");
  if (moder[0]) {
    cheat_buf_puts(&out, ",\"credits\":[\"");
    cheat_buf_puts_json(&out, moder, strlen(moder));
    cheat_buf_puts(&out, "\"]");
  }
  cheat_buf_puts(&out, "}");
  return out.buf;
}

void
read_cheat_display_name(const char *path, int kind, char *out, size_t out_size) {
  out[0] = '\0';
  if (!path || out_size < 2) {
    return;
  }
  char *txt = NULL;
  if (read_file_text(path, &txt) != 0 || !txt) {
    return;
  }

  if (kind == 1) {
    cJSON *root = cJSON_Parse(txt);
    if (root) {
      cJSON *name = cJSON_GetObjectItem(root, "name");
      if (cJSON_IsString(name) && name->valuestring && name->valuestring[0]) {
        snprintf(out, out_size, "%s", name->valuestring);
      }
      cJSON_Delete(root);
    }
  } else {
    const char *xml_src = txt;
    char *xml_from_mc4 = NULL;
    if (kind == 3) {
      xml_from_mc4 = mc4_decrypt_to_xml(txt, strlen(txt), NULL);
      xml_src = xml_from_mc4;
    }
    if (xml_src) {
      const char *t = strstr(xml_src, "<Trainer");
      if (t) {
        const char *end = strchr(t, '>');
        if (end) {
          char tag[1024];
          size_t tn = (size_t)(end - t);
          if (tn < sizeof(tag)) {
            size_t alen = 0;
            memcpy(tag, t, tn);
            tag[tn] = '\0';
            const char *v = xml_find_attr(tag, "Game", &alen);
            if (!v) {
              v = xml_find_attr(tag, "GameName", &alen);
            }
            if (v && alen > 0 && alen < out_size) {
              memcpy(out, v, alen);
              out[alen] = '\0';
            }
          }
        }
      }
    }
    free(xml_from_mc4);
  }
  free(txt);
}
