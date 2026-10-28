/* Copyright (C) 2026 HyperHEN contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "hyperdeck_model.hpp"
#include <vector>

namespace hyperdeck {

enum Ink { Surface, Raised, Line, Text, Muted, Accent, Violet, Green, Amber, Red, Deep };
struct Color { float r, g, b, a; };
inline Color color(Theme theme, int ink) {
  constexpr Color palette[] = {
    {.025f,.035f,.065f,.96f}, {.055f,.075f,.12f,.94f}, {.15f,.20f,.29f,1},
    {.94f,.97f,1,1}, {.52f,.61f,.72f,1}, {.16f,.89f,1,1}, {.65f,.43f,1,1},
    {.30f,.94f,.68f,1}, {1,.73f,.30f,1}, {1,.34f,.43f,1}, {.015f,.022f,.044f,1}
  };
  Color c = palette[std::clamp(ink, 0, 10)];
  if (theme == Theme::Reactor && ink == Accent) c = palette[Violet];
  if (theme == Theme::Blackout) {
    if (ink == Surface) c = {.015f,.015f,.02f,1};
    if (ink == Raised) c = {.055f,.055f,.065f,1};
    if (ink == Accent) c = palette[Text];
    if (ink == Line) c = {.20f,.20f,.23f,1};
  }
  return c;
}
inline int state_ink(State s) {
  return s == State::Ready ? Green : s == State::Failed ? Red :
         s == State::Waiting ? Amber : Muted;
}
struct Rect {
  float x, y, w, h;
  int ink;
  bool operator==(const Rect &) const = default;
};
struct Label {
  float x, y, w;
  int size, ink;
  std::string text;
  bool operator==(const Label &) const = default;
};
struct Layout {
  std::vector<Rect> rects;
  std::vector<Label> labels;
};
inline constexpr float kWidth = 1340, kHeight = 740;
inline constexpr size_t kMaxRects = 96, kMaxLabels = 80;

inline Layout layout(const Snapshot &s, const Navigation &nav, uint64_t now,
                     uint64_t opened_ms, std::string notice = {}) {
  Layout l;
  const auto rect = [&](float x, float y, float w, float h, int ink) {
    l.rects.push_back({x,y,w,h,ink});
  };
  const auto text = [&](float x, float y, float w, int size, int ink, std::string value) {
    l.labels.push_back({x,y,w,size,ink,std::move(value)});
  };
  const State overall = overall_state(s, now);
  rect(0,0,kWidth,kHeight,Surface);
  rect(0,0,4,kHeight,Accent);
  rect(4,0,1336,2,Line);
  // Original HyperDeck geometry retained by the standalone web application.
  rect(34,33,8,36,Accent); rect(64,33,8,36,Violet); rect(42,47,22,8,Accent);
  text(92,29,390,32,Text,"HYPERHEN");
  text(94,69,410,14,Muted,"H Y P E R D E C K   /   " + s.version);
  rect(1068,36,234,36,Raised);
  rect(1084,49,8,8,state_ink(overall));
  text(1106,43,184,16,state_ink(overall),
       overall == State::Ready ? "SYSTEM READY" : overall == State::Failed ?
       "NEEDS ATTENTION" : overall == State::Unknown ? "NO FRESH SAMPLE" : "CHECKING SERVICES");
  rect(34,108,1268,1,Line);
  constexpr const char *tabs[] = {"OVERVIEW", "REACTOR", "QUICK CONTROLS", "APPEARANCE"};
  for (int i=0;i<4;++i) {
    const float x=34+i*317;
    const bool active=i==static_cast<int>(nav.tab);
    if (active) rect(x,124,300,42,Raised);
    text(x+16,135,275,16,active?Accent:Muted,tabs[i]);
    rect(x,172,300,active?3:1,active?Accent:Line);
  }
  if (nav.tab == Tab::Home) {
    text(34,201,900,38,Text,"Your console. Supercharged.");
    text(36,253,900,17,Muted,"FW " + firmware_text(s.firmware) + "    /    " + route_text(s.firmware,s.boot_mode_5xx));
    rect(1034,203,268,70,Raised);
    text(1052,215,232,13,Muted,"LOCAL NETWORK");
    text(1052,241,232,20,Text,s.ip);
    constexpr const char *names[] = {"Payloads", "Plugins", "Cheats", "Quick controls", "Reactor", "Toolbox"};
    constexpr const char *details[] = {"Your ELF library", "Manage runtime modules", "Open the cheat library",
                                      "FTP / logs / ELF loader", "Inspect the active pipeline", "All settings and tools"};
    for (int i=0;i<6;++i) {
      const float x=34+(i%3)*428, y=301+(i/3)*143;
      const bool active=nav.selected==i;
      rect(x,y,412,127,active?Line:Raised);
      if (active) { rect(x,y,3,127,Accent); rect(x+3,y,409,2,Accent); }
      text(x+20,y+14,340,13,active?Accent:Muted,"0"+std::to_string(i+1)+"  /  HYPERHEN");
      text(x+20,y+43,370,27,Text,names[i]);
      text(x+20,y+91,370,16,Muted,details[i]);
    }
    rect(34,606,1268,58,Deep);
    const std::string values[] = {"CPU  "+s.cpu,"SOC  "+s.soc,"FAN  "+s.fan,"IP  "+s.ip};
    for (int i=0;i<4;++i) text(52+i*317,626,290,18,i==3?Accent:Text,values[i]);
  } else if (nav.tab == Tab::Reactor) {
    text(34,201,830,34,Text,"Inside HyperCore.");
    text(36,249,1000,16,Muted,"FW " + firmware_text(s.firmware) + "  /  " + route_text(s.firmware,s.boot_mode_5xx));
    const bool fresh = s.fresh && now >= s.sampled_ms && now-s.sampled_ms<=3500;
    for (size_t i=0;i<s.stage_count;++i) {
      const float y=290+static_cast<float>(i)*65;
      const State state=fresh?s.stages[i].state:State::Unknown;
      rect(34,y,1268,56,Raised);
      rect(34,y,3,56,state_ink(state));
      text(54,y+17,55,17,Muted,"0"+std::to_string(i+1));
      text(115,y+15,250,21,Text,s.stages[i].name);
      text(380,y+19,660,16,Muted,s.stages[i].detail);
      text(1125,y+18,160,17,state_ink(state),state_text(state));
    }
    text(36,628,1010,15,Muted,"Live observations; completion markers do not certify every firmware feature.");
    rect(1083,618,219,44,Line);
    text(1104,632,185,16,Accent,"X  Refresh readings");
  } else if (nav.tab == Tab::Quick) {
    text(34,201,900,34,Text,"One place. Full control.");
    text(36,249,1100,16,Muted,"Service switches reflect recorded live processes. Give launches a moment to complete.");
    constexpr const char *names[] = {"Kernel log server", "FTP server", "ELF loader", "Telemetry overlay", "Linux next boot", "Open Toolbox"};
    constexpr const char *details[] = {"TCP 3232", "TCP 2121", "TCP 9025", "Use your configured HUD", "Configure the clean-boot loader", "All HyperHEN settings"};
    for (int i=0;i<6;++i) {
      const float y=289+i*61;
      const bool focus=i==nav.selected;
      rect(34,y,1268,53,focus?Line:Raised);
      if(focus) rect(34,y,3,53,Accent);
      text(55,y+16,470,21,Text,names[i]);
      text(543,y+20,455,16,Muted,details[i]);
      if(i<4) {
        const bool on=i<3?s.tools[i]:s.overlay;
        rect(1194,y+14,82,27,on?Accent:Deep);
        text(1214,y+19,61,14,on?Deep:Muted,on?"ON":"OFF");
      } else text(1203,y+17,85,16,Accent,"OPEN");
    }
  } else {
    text(34,201,900,34,Text,"Make it yours.");
    text(36,249,1100,16,Muted,"Your HyperDeck. Three palettes. Motion only while opening or navigating.");
    constexpr const char *themes[] = {"HyperGlass", "Reactor", "Blackout"};
    constexpr const char *subtitles[] = {"Cyan / smoked glass", "Ultraviolet / dark energy", "Monochrome / solid panels"};
    for(int i=0;i<3;++i) {
      const float x=34+i*428;
      const bool focus=nav.selected==i;
      rect(x,301,412,190,focus?Line:Raised);
      rect(x+20,323,112,6,i==1?Violet:i==2?Text:Accent);
      text(x+20,359,370,28,Text,themes[i]);
      text(x+20,406,370,16,Muted,subtitles[i]);
      text(x+20,451,370,15,nav.theme==static_cast<Theme>(i)?Green:Muted,
           nav.theme==static_cast<Theme>(i)?"SELECTED":"X  Select");
    }
    rect(34,522,1268,72,nav.selected==3?Line:Raised);
    text(54,544,530,23,Text,"Reduced motion");
    text(590,549,510,16,Muted,"Stored with your HyperDeck appearance");
    text(1194,548,91,18,Accent,nav.reduced_motion?"ON":"OFF");
  }
  rect(34,686,1268,1,Line);
  text(36,705,880,15,Muted,notice.empty()?"L1 / R1  Sections      D-pad  Navigate      X  Select      Circle  Back":notice);
  text(1022,705,280,13,Accent,"CODED BY MICHELE MEDIA");
  const float dx = reveal_offset(now >= opened_ms ? now-opened_ms : 240, nav.reduced_motion);
  if (dx) { for(auto &r:l.rects) r.x+=dx; for(auto &t:l.labels) t.x+=dx; }
  return l;
}

} // namespace hyperdeck
