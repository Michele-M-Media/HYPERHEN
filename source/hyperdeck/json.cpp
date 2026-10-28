/* HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "hyperdeck_backend.hpp"
namespace hyperdeck {
std::string json_quote(std::string_view value) {
  constexpr char hex[]="0123456789abcdef";
  std::string out="\"";
  for (const unsigned char c:value) {
    if(c=='"' || c=='\\') {out+='\\';out+=static_cast<char>(c);}
    else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
    else out+=static_cast<char>(c);
  }
  return out+'"';
}
}
