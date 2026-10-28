#ifndef CR_CHEAT_FORMATS_H
#define CR_CHEAT_FORMATS_H

#include <stddef.h>

int recognised_cheat_extension(const char *name);
const char *xml_find_attr(const char *node, const char *attr, size_t *len_out);
char *mc4_decrypt_to_xml(const char *cipher, size_t cipher_len, size_t *xml_size_out);
char *shn_xml_to_json(const char *xml, size_t xml_len);
void read_cheat_display_name(const char *path, int kind, char *out, size_t out_size);

#endif
