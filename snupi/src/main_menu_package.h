#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace snupi::startup {
// Bounds-checked v25 editing. Keep export IDs, offsets and all unrelated bytes;
// only shorten property arrays or replace values of the same size.
class MainMenuPackage {
 public:
  struct Export { std::string name, type; uint32_t parent, offset, size, table; };
  explicit MainMenuPackage(std::vector<uint8_t>& bytes) : data(bytes) {
    if (Word(0) != 0x9e2a83c1 || Word(4) != 25) Fail();
    auto count = Word(16), pos = Word(20);
    if (count > data.size() / 3) Fail();
    for (uint32_t i = 0; i < count; ++i) {
      const auto length = Half(pos); pos += 2;
      Bounds(pos, length);
      if (!length || data[pos + length - 1]) Fail();
      names.emplace_back(reinterpret_cast<const char*>(data.data() + pos), length - 1);
      pos += length;
    }
    pos = Word(32); count = Word(pos); pos += 4;
    if (count > data.size() / 49) Fail();
    exports.push_back({});
    for (uint32_t i = 0; i < count; ++i) {
      Bounds(pos, 49);
      Export e{Name(Half(pos)), Name(Half(pos + 4)), Word(pos + 8),
               Word(pos + 24), Word(pos + 20), pos};
      Bounds(e.offset, e.size);
      if (e.size < 16) Fail();
      exports.push_back(e);
      const uint64_t next = uint64_t(pos) + 49 + uint64_t(Word(pos + 44)) * 8;
      Bounds(pos, next - pos); pos = uint32_t(next);
    }
    if (pos != Word(12)) Fail();
  }
  uint32_t Find(const std::string& name, const std::string& type, uint32_t parent = 0) const {
    uint32_t result = 0;
    for (uint32_t i = 1; i < exports.size(); ++i) {
      const auto& e = exports[i];
      if (e.name == name && e.type == type && e.parent == parent) {
        if (result) Fail();
        result = i;
      }
    }
    if (!result) Fail();
    return result;
  }
  void Edit(uint32_t id, const std::string& property,
            const std::function<std::vector<uint8_t>(std::vector<uint8_t>)>& change,
            bool erase = false) {
    if (!id || id >= exports.size()) Fail();
    auto& e = exports[id];
    size_t pos = e.offset + 12, end = e.offset + e.size;
    std::vector<uint8_t> out(data.begin() + e.offset, data.begin() + pos);
    bool found = false;
    while (true) {
      if (pos + 4 > end) Fail();
      const auto name = Half(pos);
      if (!name) { if (pos + 4 != end) Fail(); break; }
      if (pos + 14 > end) Fail();
      const auto size = Word(pos + 8);
      if (size > end - pos - 14) Fail();
      std::vector<uint8_t> value(data.begin() + pos + 14, data.begin() + pos + 14 + size);
      if (Name(name) == property) {
        if (found) Fail();
        found = true; value = change(std::move(value));
        if (erase) { pos += 14 + size; continue; }
      }
      const auto header = out.size();
      out.insert(out.end(), data.begin() + pos, data.begin() + pos + 14);
      Put(out, header + 8, uint32_t(value.size()));
      out.insert(out.end(), value.begin(), value.end());
      pos += 14 + size;
    }
    if (!found && !erase) Fail();
    out.insert(out.end(), 4, 0);
    if (out.size() > e.size) Fail();
    std::fill(data.begin() + e.offset, data.begin() + e.offset + e.size, 0);
    std::copy(out.begin(), out.end(), data.begin() + e.offset);
    e.size = uint32_t(out.size());
    Put(data, e.table + 20, e.size); Put(data, e.table + 28, e.size);
  }
  void Erase(uint32_t id, const std::string& property) {
    Edit(id, property, [](auto bytes) { return bytes; }, true);
  }
  void Set(uint32_t id, const std::string& property, uint32_t value) {
    Edit(id, property, [value](auto bytes) {
      if (bytes.size() != 4) Fail();
      Put(bytes, 0, value); return bytes;
    });
  }
  void Remove(uint32_t id, const std::string& property, const std::vector<uint32_t>& removed) {
    Edit(id, property, [&](auto bytes) {
      if (bytes.size() < 4 || bytes.size() != 4ull + uint64_t(Get(bytes, 0)) * 4) Fail();
      std::vector<uint8_t> result(4);
      for (size_t i = 4; i < bytes.size(); i += 4) {
        if (std::find(removed.begin(), removed.end(), Get(bytes, i)) == removed.end())
          result.insert(result.end(), bytes.begin() + i, bytes.begin() + i + 4);
      }
      Put(result, 0, uint32_t(result.size() / 4 - 1)); return result;
    });
  }
  uint32_t Ref(uint32_t id, const std::string& property) {
    uint32_t value = 0;
    Edit(id, property, [&](auto bytes) {
      if (bytes.size() != 4) Fail();
      value = Get(bytes, 0); return bytes;
    });
    return value;
  }
  void SetVectorY(uint32_t id, const std::string& property, float y) {
    Edit(id, property, [&](auto bytes) {
      size_t pos = 0;
      bool found = false;
      while (pos + 4 <= bytes.size()) {
        const auto name = uint16_t(bytes[pos]) << 8 | bytes[pos + 1];
        if (!name) break;
        if (pos + 14 > bytes.size()) Fail();
        const auto size = Get(bytes, pos + 8);
        if (size > bytes.size() - pos - 14) Fail();
        if (Name(name) == "y") {
          if (found || size != 4) Fail();
          Put(bytes, pos + 14, std::bit_cast<uint32_t>(y)); found = true;
        }
        pos += 14 + size;
      }
      if (!found || pos + 4 != bytes.size()) Fail();
      return bytes;
    });
  }
  const std::vector<Export>& Exports() const { return exports; }
 private:
  [[noreturn]] static void Fail() { throw std::runtime_error("-hidelive: unsupported FrontEnd.pkg layout"); }
  void Bounds(uint64_t offset, uint64_t count) const {
    if (offset > data.size() || count > data.size() - offset) Fail();
  }
  static uint32_t Get(const std::vector<uint8_t>& v, size_t p) {
    if (p > v.size() || v.size() - p < 4) Fail();
    return uint32_t(v[p]) << 24 | uint32_t(v[p+1]) << 16 | uint32_t(v[p+2]) << 8 | v[p+3];
  }
  static void Put(std::vector<uint8_t>& v, size_t p, uint32_t x) {
    if (p > v.size() || v.size() - p < 4) Fail();
    for (int i = 0; i < 4; ++i) v[p+i] = uint8_t(x >> (24 - i*8));
  }
  uint32_t Word(size_t p) const { return Get(data, p); }
  uint16_t Half(size_t p) const { Bounds(p, 2); return uint16_t(data[p]) << 8 | data[p+1]; }
  const std::string& Name(size_t index) const { if (index >= names.size()) Fail(); return names[index]; }
  std::vector<uint8_t>& data;
  std::vector<std::string> names;
  std::vector<Export> exports;
};

inline void PatchMainMenuReturnAnimation(std::vector<uint8_t>& data) {
  MainMenuPackage p(data);
  const auto main = p.Find("m_mainMenu", "Interface");
  const auto enlist = p.Find("m_enlist_now", "Interface");
  unsigned count = 0;
  for (uint32_t i = 1; i < p.Exports().size(); ++i) {
    const auto& e = p.Exports()[i];
    if (e.type != "InterfaceAction_DestroyThisInterface") continue;
    auto parent = e.parent;
    for (size_t depth = 0; parent && parent != enlist && depth < p.Exports().size(); ++depth) {
      if (parent >= p.Exports().size()) throw std::runtime_error("Invalid menu action parent");
      parent = p.Exports()[parent].parent;
    }
    if (parent != enlist || p.Ref(i, "m_nextInterface") != main) continue;
    // The shipped Back action requests eILoad_Named / Show, but mainMenu has
    // no Show mode. Use the default loading path (Load -> Default), which
    // initializes the offscreen position and blends in using native timing.
    p.Erase(i, "m_loadCmd");
    p.Erase(i, "m_loadMode");
    ++count;
  }
  if (count != 1) throw std::runtime_error("Unexpected Enlist return action count");
}

inline void PatchMainMenuPackage(std::vector<uint8_t>& data) {
  MainMenuPackage p(data);
  const auto root = p.Find("m_mainMenu", "Interface");
  auto button = [&](const char* name) { return p.Find(name, "Element_2DInterfaceButton", root); };
  const auto live = button("btn_multiplayer"), boards = button("btn_leaderboards");
  const auto downloads = button("btn_downloadContent");
  const auto enlist = button("btn_singlePlayer");
  const auto badge = p.Find("newDLCavailable", "Element_2DInterface", root);
  p.Remove(root, "m_aRenderList", {live, boards, downloads, badge});
  const auto selections = p.Find("manager_selections", "Element_2DElementManager", root);
  const std::vector<uint32_t> chain{enlist, button("btn_achievements"),
      button("btn_helpAndOptions"), button("btn_unlockFullGame"), button("btn_exitGame")};
  p.Edit(selections, "m_aChildren", [&](auto) {
    std::vector<uint8_t> bytes;
    auto append = [&](uint32_t value) {
      for (int b = 0; b < 4; ++b) bytes.push_back(uint8_t(value >> (24 - b * 8)));
    };
    append(uint32_t(chain.size()));
    for (auto id : chain) append(id);
    return bytes;
  });
  p.Set(selections, "m_vertical", 5);
  const auto fixed = p.Find("manager_unchange", "Element_2DElementManager", root);
  p.Remove(fixed, "m_aChildren", {live, enlist});
  p.Set(enlist, "m_pParent", selections);
  // One dynamic group packs the four full-game buttons without the old LIVE
  // group or its separator. The native license logic still controls Unlock.
  const auto separator = p.Find("dashed_line", "Element_2DPlane", root);
  p.Remove(root, "m_aRenderList", {separator});
  p.Remove(p.Find("manager_ALL", "Element_2DElementManager", root),
           "m_aChildren", {fixed, separator});
  const auto background = p.Find("background", "Element_2DPlane", root);
  auto compact = [&](const char* aspect, float top, float rowHalfHeight) {
    const float area = 4.0f * rowHalfHeight;
    const float panel = area + 0.08f;
    const float center = top - panel;
    auto state = p.Find(aspect, "Element_2DElementManagerStateData",
                        p.Ref(selections, "m_pDefaultState"));
    p.SetVectorY(p.Ref(state, "Area"), "m_Value", area);
    p.SetVectorY(p.Ref(state, "Position"), "m_Value", center);
    state = p.Find(aspect, "Element_2DPlaneStateData", p.Ref(background, "m_pDefaultState"));
    p.SetVectorY(p.Ref(state, "Scale"), "m_Value", panel / 0.7250000238418579f);
    p.SetVectorY(p.Ref(state, "Position"), "m_Value", center);
    // safe_BL is inset 10% from the viewport bottom. Move the whole group,
    // including its title tab, so the paper's lower edge reaches the viewport.
    // Apply the same Y to slide states to avoid a vertical jump on transitions.
    const auto manager = p.Find("manager_ALL", "Element_2DElementManager", root);
    const float bottomAlignedY = panel - center - 0.1f;
    for (const char* name : {"Default", "HiddenToDefault", "DefaultToHidden", "Unload", "Load"}) {
      // The 4:3 transition states inherit their position from Default.
      if (std::string(aspect) == "4x3" && std::string(name) != "Default") continue;
      const auto animation = p.Find(name, "Element_2DElementManagerState", manager);
      const auto layout = p.Find(aspect, "Element_2DElementManagerStateData", animation);
      p.SetVectorY(p.Ref(layout, "Position"), "m_Value", bottomAlignedY);
    }
  };
  // Keep the local top edge and row pitch in both aspect layouts. Resize
  // only the paper background and layout area, never the button/text scale.
  compact("4x3", -0.16244739294052124f + 0.7250000238418579f,
          0.5546875f / 5.0f);
  compact("16x9", -0.3075856864452362f + 0.7250000238418579f * 1.4271445274353027f,
          0.4513888955116272f / 5.0f);
  for (size_t i = 0; i < chain.size(); ++i) {
    p.Set(chain[i], "m_upElement", chain[(i + chain.size() - 1) % chain.size()]);
    p.Set(chain[i], "m_downElement", chain[(i + 1) % chain.size()]);
  }
  // Startup/return-to-menu selection must not point at the hidden Xbox LIVE
  // button. Scope the action to this interface; leave multiplayer screens alone.
  for (uint32_t i = 1; i < p.Exports().size(); ++i) {
    const auto& e = p.Exports()[i];
    if (e.type != "InterfaceAction_SetActiveElement") continue;
    auto parent = e.parent;
    for (size_t depth = 0; parent && parent != root && depth < p.Exports().size(); ++depth) {
      if (parent >= p.Exports().size()) throw std::runtime_error("Invalid menu action parent");
      parent = p.Exports()[parent].parent;
    }
    if (parent == root) {
      p.Edit(i, "m_element", [=](auto bytes) {
        if (bytes.size() != 4) throw std::runtime_error("Invalid menu selection action");
        const auto id = uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 |
                        uint32_t(bytes[2]) << 8 | bytes[3];
        if (id == live || id == boards || id == downloads)
          for (int b = 0; b < 4; ++b) bytes[b] = uint8_t(enlist >> (24 - b * 8));
        return bytes;
      });
    }
  }
}
}
