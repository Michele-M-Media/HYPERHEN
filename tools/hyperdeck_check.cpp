/* Copyright (C) 2026 HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "hyperdeck_layout.hpp"
#include "onpress_policy.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace hyperdeck;

static Snapshot fixture(uint32_t fw=0x10010000U) {
  Snapshot s;
  s.firmware=fw; s.version="v0.9.2 / UI preview";
  describe_stages(s);
  for(size_t i=0;i<s.stage_count;++i) s.stages[i].state=State::Ready;
  s.ip="192.168.1.50"; s.cpu="57 C"; s.soc="60 C"; s.fan="35%";
  s.tools={true,false,true}; s.overlay=true; s.fresh=true; s.sampled_ms=1000;
  return s;
}

static void verify() {
  assert(firmware_text(0x10010000U)=="10.01");
  assert(firmware_text(0x07610000U)=="7.61");
  assert(firmware_text(0x13420000U)=="13.42");
  assert(observed_state(true,true)==State::Failed);
  assert(observed_state(false,false)==State::Unknown);
  assert(observed_state(false,false,true)==State::Waiting);
  for(unsigned major=1;major<=0x13;++major) for(unsigned minor=0;minor<=0x99;++minor) {
    const uint32_t fw=(major<<24)|(minor<<16);
    Snapshot s=fixture(fw);
    const auto route=onion_firmware_route(fw);
    const uint32_t caps=onion_firmware_capabilities(route);
    if(route==ONION_FIRMWARE_ROUTE_UNSUPPORTED) {
      assert(s.stage_count==0); assert(overall_state(s,1000)==State::Unknown);
      continue;
    }
    assert(s.stage_count>=2 && s.stage_count<=5);
    bool ui=false,hv=false;
    for(size_t i=0;i<s.stage_count;++i) {
      ui|=std::string(s.stages[i].name)=="HYPERUI";
      hv|=std::string(s.stages[i].name)=="BYEPERVISOR";
    }
    assert(ui==!!(caps & ONION_FIRMWARE_CAP_TOOLBOX));
    assert(hv==!!(caps & ONION_FIRMWARE_CAP_HYPERVISOR));
    assert(overall_state(s,1000)==State::Ready);
    assert(overall_state(s,4501)==State::Unknown);
    assert(overall_state(s,999)==State::Unknown);
    s.stages[0].state=State::Failed; assert(overall_state(s,1000)==State::Failed);
    s.stages[0].state=State::Unknown; assert(overall_state(s,1000)==State::Waiting);
    for(int tab=0;tab<4;++tab) for(int theme=0;theme<3;++theme)
      for(int selected=0;selected<item_count(static_cast<Tab>(tab));++selected) {
        Navigation n; n.tab=static_cast<Tab>(tab); n.theme=static_cast<Theme>(theme); n.selected=selected;
        const auto l=layout(s,n,2000,1000);
        assert(l.rects.size()<=kMaxRects && l.labels.size()<=kMaxLabels);
        for(const auto &r:l.rects) {
          assert(std::isfinite(r.x) && std::isfinite(r.y));
          assert(r.x>=0 && r.y>=0 && r.w>0 && r.h>0);
          assert(r.x+r.w<=kWidth && r.y+r.h<=kHeight);
          assert(r.ink>=0 && r.ink<=10);
        }
        for(const auto &t:l.labels) {
          assert(t.x>=0 && t.y>=0 && t.w>0 && t.x+t.w<=kWidth);
          assert(t.y+t.size+12<=kHeight && t.ink>=0 && t.ink<=10);
        }
      }
  }
  Snapshot unknown=fixture(0x14000000U);
  assert(unknown.stage_count==0);
  Navigation n;
  assert(n.input(128,0)==Action::Payloads);
  assert(n.input(128,100)==Action::None); // Held X never repeats a service action.
  n.input(0,200); n.input(8,250); assert(n.selected==3);
  n.input(0,300); n.input(4,350); assert(n.selected==4);
  n.input(0,400); n.input(2048,450); assert(n.tab==Tab::Reactor && n.selected==0);
  n.input(2048,500); assert(n.tab==Tab::Reactor);
  n.input(0,550); assert(n.input(64,600)==Action::Back);
  assert(reveal_offset(240,false)==0 && reveal_offset(10000,false)==0);
  assert(reveal_offset(0,true)==0 && reveal_offset(0,false)>0);
  assert(action_at(Tab::Quick,-1)==Action::None);
  assert(action_at(Tab::Quick,6)==Action::None);
  std::cerr<<"HyperDeck: profile/state, stale data, input, motion and layout checks passed.\n";
}

static void json(const Layout &l) {
  std::cout<<"{\"rects\":[";
  bool first=true;
  for(const auto &r:l.rects) {
    if(!first) std::cout<<','; first=false;
    std::cout<<'['<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<','<<r.ink<<']';
  }
  std::cout<<"],\"labels\":["; first=true;
  for(const auto &t:l.labels) {
    if(!first) std::cout<<','; first=false;
    std::cout<<'['<<t.x<<','<<t.y<<','<<t.w<<','<<t.size<<','<<t.ink<<','<<std::quoted(t.text)<<']';
  }
  std::cout<<"]}";
}

int main(int argc,char **argv) {
  verify();
  if(argc<2 || std::string(argv[1])!="--preview") return 0;
  std::cout<<"window.HYPERDECK_PREVIEW={\"palettes\":[";
  for(int theme=0;theme<3;++theme) {
    if(theme) std::cout<<','; std::cout<<'[';
    for(int ink=0;ink<11;++ink) {
      if(ink) std::cout<<',';
      const Color c=color(static_cast<Theme>(theme),ink);
      std::cout<<'['<<c.r<<','<<c.g<<','<<c.b<<','<<c.a<<']';
    }
    std::cout<<']';
  }
  std::cout<<"],\"frames\":{";
  bool first=true;
  for(int profile=0;profile<3;++profile) for(int scenario=0;scenario<3;++scenario)
    for(int tab=0;tab<4;++tab) for(int theme=0;theme<3;++theme)
      for(int selected=0;selected<item_count(static_cast<Tab>(tab));++selected) {
        Snapshot s=fixture(profile==0?0x10010000U:profile==1?0x13200000U:0x12700000U);
        if(scenario==1) s.stages[1].state=State::Failed;
        if(scenario==2) {s.fresh=false;s.cpu="--";s.soc="--";s.fan="--";s.ip="--";}
        Navigation n; n.tab=static_cast<Tab>(tab); n.theme=static_cast<Theme>(theme);n.selected=selected;
        if(!first) std::cout<<',';first=false;
        const std::string key=std::to_string(profile)+":"+std::to_string(scenario)+":"+
            std::to_string(tab)+":"+std::to_string(theme)+":"+std::to_string(selected);
        std::cout<<std::quoted(key)<<':'; json(layout(s,n,2000,1000));
      }
  std::cout<<"}};\n";
}
