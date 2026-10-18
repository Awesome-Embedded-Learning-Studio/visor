#pragma once

#include <cstdint>
#include <optional>

#include "visor/base/bit_ops/bitmask.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"

namespace visor::host::evdev {

/// The three fields translation reads from Linux's struct input_event. The
/// timestamp is deliberately absent: its two timeval words are 8 bytes wide
/// on 64-bit Linux and 4 on 32-bit, while the translator never reads them.
/// The host adapter copies type, code and value out of the kernel word; the
/// clock stays the host table's job. Mirroring only what is read keeps this
/// layer free of a platform-dependent layout and of the kernel headers.
struct WireEvent {
    std::uint16_t type = 0;
    std::uint16_t code = 0;
    std::int32_t value = 0;
};

// Event types (linux/input-event-codes.h).
inline constexpr std::uint16_t kEvSyn = 0x00;
inline constexpr std::uint16_t kEvKey = 0x01;
inline constexpr std::uint16_t kEvRel = 0x02;

// Relative axes.
inline constexpr std::uint16_t kRelX = 0x00;
inline constexpr std::uint16_t kRelY = 0x01;
inline constexpr std::uint16_t kRelHWheel = 0x06;
inline constexpr std::uint16_t kRelWheel = 0x08;

// Mouse buttons. The kernel puts buttons at 0x100 and above; keyboard key
// codes stay below that band.
inline constexpr std::uint16_t kBtnLeft = 0x110;
inline constexpr std::uint16_t kBtnRight = 0x111;
inline constexpr std::uint16_t kBtnMiddle = 0x112;

// The key codes the core names, as evdev numbers. Backspace, Tab, Enter and
// Space already share the core's values. The arrows do not: evdev's 103 to
// 108 sit inside printable ASCII, so the translator maps them onto the core's
// out-of-band values, the same collision B16 settled for every host.
inline constexpr std::uint16_t kKeyBackspace = 14;
inline constexpr std::uint16_t kKeyTab = 15;
inline constexpr std::uint16_t kKeyEnter = 28;
inline constexpr std::uint16_t kKeySpace = 57;
inline constexpr std::uint16_t kKeyUp = 103;
inline constexpr std::uint16_t kKeyLeft = 105;
inline constexpr std::uint16_t kKeyRight = 106;
inline constexpr std::uint16_t kKeyDown = 108;

// The modifier keys the state tracks. Left and right halves set the same
// named flag; the core does not care which hand held it.
inline constexpr std::uint16_t kKeyLeftCtrl = 29;
inline constexpr std::uint16_t kKeyLeftShift = 42;
inline constexpr std::uint16_t kKeyRightShift = 54;
inline constexpr std::uint16_t kKeyLeftAlt = 56;
inline constexpr std::uint16_t kKeyRightCtrl = 97;
inline constexpr std::uint16_t kKeyRightAlt = 100;
inline constexpr std::uint16_t kKeyLeftMeta = 125;
inline constexpr std::uint16_t kKeyRightMeta = 126;

/**
 * @brief The state a Linux input stream does not repeat in every word: the
 *        pointer position relative axes accumulate into, and the modifiers
 *        pressed so far. One state lives per input device for the adapter's
 *        lifetime.
 *
 * The starting pointer is (0, 0), the top-left of the screen. Relative moves
 * are clamped to the screen size the state was built with, so a pointer that
 * runs past an edge stops there and comes back on the next reversal. An empty
 * screen clamps every move to (0, 0).
 */
class EvdevState {
  public:
    explicit EvdevState(base::SizeI screen) noexcept;

  private:
    friend std::optional<host::Event> translate_event(EvdevState& state,
                                                      const WireEvent& wire) noexcept;

    /// Sets or clears the bit for one physical modifier key and recomputes
    /// the named flags. Left and right halves are separate bits so releasing
    /// one hand does not clear a flag the other hand still holds.
    void set_modifier(std::uint16_t code, bool pressed) noexcept;

    base::SizeI screen_;
    base::PointI pointer_;
    host::Modifiers modifiers_;
    base::bit::BitMask<std::uint8_t> held_modifiers_;
};

/**
 * @brief Translates one Linux input word into the core's Event, pure logic.
 *
 * EV_KEY with value 1 is a press, 0 a release, 2 an auto-repeat that still
 * reads as a press (a control decides whether it is already held). A modifier
 * key updates the state before its own event is emitted, so the word that
 * follows carries the new flags. The mouse buttons map to buttons 1 to 3 at
 * the tracked pointer position; a repeat on a button is dropped. Other keys
 * pass their evdev code through unchanged, except the arrows, which take the
 * core's out-of-band values. This layer does not produce text: letters and
 * digits stay evdev codes, and a text control sees the space key as 57, not
 * as the character 32. A keymap layer is the later consumer for that.
 *
 * EV_REL moves the tracked pointer one axis at a time and emits a PointerMove
 * for each word. REL_WHEEL and REL_HWHEEL become one wheel word each, the
 * value taken as detents and the sign normalized to the core's convention
 * (wheel up is negative dy, right is positive dx). EV_SYN, relative axes
 * outside those four, unknown event types and EV_KEY values outside 0 to 2
 * translate to nothing, the same honest refusal as an empty table entry.
 *
 * "No event" is a normal answer, not a failure. Every such word leaves the
 * adapter with the same action -- drop it -- so the return type stays optional
 * rather than growing an error channel with no decision behind it. The one
 * word that would earn a different answer is SYN_DROPPED (EV_SYN with code
 * 3), where the kernel has lost events and the tracked pointer and modifiers
 * may be stale. This layer drops it like any other synchronization word and
 * does not rebuild the state yet.
 */
[[nodiscard]] std::optional<host::Event> translate_event(EvdevState& state,
                                                         const WireEvent& wire) noexcept;

} // namespace visor::host::evdev
