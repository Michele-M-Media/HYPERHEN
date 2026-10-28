/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exact game/version validation follows TacHen src/game-metadata.mjs.
 */
#include <onion/plugin_metadata.hpp>
#include <cJSON.hpp>
#include <algorithm>
#include <cstring>
#include <set>
#include <vector>
namespace onion::sprx {
namespace {
template<class T> T get(std::span<const uint8_t> bytes, size_t at) {
  T v{}; if(at <= bytes.size() && sizeof(v) <= bytes.size()-at) memcpy(&v, bytes.data()+at, sizeof(v)); return v;
}
bool text(std::span<const uint8_t> bytes, std::string &out) {
  const auto zero=std::find(bytes.begin(), bytes.end(), uint8_t{0});
  if(zero==bytes.end() || !std::all_of(zero, bytes.end(), [](uint8_t v){return v==0;})) return false;
  out.assign(reinterpret_cast<const char *>(bytes.data()), static_cast<size_t>(zero-bytes.begin())); return true;
}
}
bool parse_game_metadata(std::span<const uint8_t> bytes, std::string_view title,
                         std::string &version) noexcept {
  version.clear();
  if(bytes.size()<20 || bytes.size()>1024*1024 || title.size()!=9) return false;
  const bool ps5=title.starts_with("PPS") || title.starts_with("MOUU");
  std::string actual;
  if(ps5) {
    std::string json(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    const auto zero=json.find('\0');
    if(zero!=std::string::npos) {
      if(!std::all_of(json.begin()+static_cast<ptrdiff_t>(zero),json.end(),[](char c){return !c;})) return false;
      json.resize(zero);
    }
    cJSON *root=cJSON_ParseWithOpts(json.c_str(),nullptr,1);
    if(!root) return false;
    unsigned ids=0,versions=0;
    if(cJSON_IsObject(root)) for(auto *item=root->child;item;item=item->next) {
      if(item->string && !strcmp(item->string,"titleId")) {++ids;if(cJSON_IsString(item))actual=item->valuestring;}
      if(item->string && !strcmp(item->string,"contentVersion")) {++versions;if(cJSON_IsString(item))version=item->valuestring;}
    }
    cJSON_Delete(root);
    if(ids!=1 || versions!=1) return false;
  } else {
    if(get<uint32_t>(bytes,0)!=0x46535000 || get<uint32_t>(bytes,4)!=0x101) return false;
    const size_t keys=get<uint32_t>(bytes,8),values=get<uint32_t>(bytes,12),count=get<uint32_t>(bytes,16);
    if(!count || count>1024 || 20+count*16>keys || keys>=values || values>bytes.size()) return false;
    std::set<std::string> names;std::vector<std::pair<size_t,size_t>> spans;
    for(size_t i=0;i<count;++i) {
      const size_t at=20+i*16,key=keys+get<uint16_t>(bytes,at),size=get<uint32_t>(bytes,at+4),
        capacity=get<uint32_t>(bytes,at+8),offset=get<uint32_t>(bytes,at+12);
      if(key>=values || size>capacity || offset>bytes.size()-values || capacity>bytes.size()-values-offset) return false;
      const auto *start=reinterpret_cast<const char *>(bytes.data()+key);
      const size_t length=strnlen(start,values-key);
      if(!length || length>=values-key || length>128) return false;
      std::string name(start,length);
      if(!std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32&&c<=126;}) || !names.insert(name).second) return false;
      if(capacity) spans.emplace_back(values+offset,values+offset+capacity);
      if(name=="TITLE_ID" || name=="APP_VER") {
        const auto format=get<uint16_t>(bytes,at+2);
        std::string field;
        if((format!=4 && format!=0x204) || !size || !text(bytes.subspan(values+offset,size),field)) return false;
        if(name=="TITLE_ID") actual=field;else version=field;
      }
    }
    std::sort(spans.begin(),spans.end());
    for(size_t i=1;i<spans.size();++i) if(spans[i].first<spans[i-1].second) return false;
  }
  if(actual!=title || version.size()!=(ps5?10u:5u)) return false;
  for(size_t i=0;i<version.size();++i) {
    const bool dot=i==2 || (ps5 && i==6);
    if(dot ? version[i]!='.' : (version[i]<'0'||version[i]>'9')) return false;
  }
  return true;
}
}
