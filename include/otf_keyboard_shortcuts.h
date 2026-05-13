#ifndef OTF_KEYBOARD_SHORTCUTS_H_
#define OTF_KEYBOARD_SHORTCUTS_H_

#include <cstdint>
#include <sstream>
#include <string>

namespace otf {

class OtfHandler;

// Windows virtual-key codes used by CEF on all platforms
namespace Key {
  constexpr int kTab       = 0x09;
  constexpr int kEscape    = 0x1B;
  constexpr int kSpace     = 0x20;
  constexpr int kLeft      = 0x25;
  constexpr int kRight     = 0x27;
  constexpr int k0         = 0x30;
  constexpr int kEquals    = 0x3D;
  constexpr int kF5        = 0x74;
  constexpr int kF6        = 0x75;
  constexpr int kNum0      = 0x60;
  constexpr int kNumAdd    = 0x6B;
  constexpr int kNumMinus  = 0x6D;

  constexpr int kF = 0x46;
  constexpr int kG = 0x47;
  constexpr int kL = 0x4C;
  constexpr int kP = 0x50;
  constexpr int kR = 0x52;
  constexpr int kT = 0x54;
  constexpr int kW = 0x57;

  constexpr int kPlus  = 0xBB;
  constexpr int kMinus = 0xBD;
}

// Modifier masks — mirror CEF's EVENTFLAG_* values
namespace Mod {
  constexpr uint32_t kNone  = 0;
  constexpr uint32_t kCtrl  = 1 << 1;   // EVENTFLAG_CONTROL_DOWN
  constexpr uint32_t kShift = 1 << 2;   // EVENTFLAG_SHIFT_DOWN
  constexpr uint32_t kAlt   = 1 << 3;   // EVENTFLAG_ALT_DOWN

  inline uint32_t Of(uint32_t cef_modifiers) {
    uint32_t m = kNone;
    if (cef_modifiers & EVENTFLAG_CONTROL_DOWN) m |= kCtrl;
    if (cef_modifiers & EVENTFLAG_SHIFT_DOWN)  m |= kShift;
    if (cef_modifiers & EVENTFLAG_ALT_DOWN)    m |= kAlt;
    return m;
  }
}

// Shortcut event names sent to the frontend
namespace Shortcut {
  inline constexpr const char* kNewTab    = "new-tab";
  inline constexpr const char* kCloseTab  = "close-tab";
  inline constexpr const char* kReopenTab = "reopen-tab";
  inline constexpr const char* kNextTab   = "next-tab";
  inline constexpr const char* kPrevTab   = "prev-tab";
  inline constexpr const char* kFocusBar  = "focus-bar";
  inline constexpr const char* kReload    = "reload";
  inline constexpr const char* kBack      = "back";
  inline constexpr const char* kForward   = "forward";
  inline constexpr const char* kEscape    = "escape";
  inline constexpr const char* kZoomIn    = "zoom-in";
  inline constexpr const char* kZoomOut   = "zoom-out";
  inline constexpr const char* kZoomReset = "zoom-reset";
  inline constexpr const char* kFind      = "find";
  inline constexpr const char* kFindNext  = "find-next";
  inline constexpr const char* kFindPrev  = "find-prev";
}

inline void SendShortcut(OtfHandler* handler, const char* name) {
  std::stringstream ss;
  ss << "{\"key\":\"shortcut\",\"value\":\"" << name << "\"}";
  handler->SendEvent(ss.str());
}

}  // namespace otf

#endif  // OTF_KEYBOARD_SHORTCUTS_H_
