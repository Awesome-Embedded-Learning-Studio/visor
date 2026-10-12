#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "image.hpp"

namespace visor::fontgen {

/**
 * @brief A parse failure with ready-to-print wording.
 *
 * A string rather than an enum: every failure wants its own sentence naming
 * the offending token or the byte counts on both sides, and no caller ever
 * branches on the kind — it prints and exits nonzero.
 */
struct PbmError {
    std::string message;
};

/**
 * @brief   Parses a binary PBM (P4) buffer into a MonoImage.
 *
 * Header: the magic "P4", then width and height as whitespace-separated
 * decimal tokens; # comments run to end of line and may appear between
 * tokens; exactly one whitespace byte separates the header from the payload.
 * Payload: height rows of (width+7)/8 bytes, MSB-first, bit 1 means ink.
 *
 * The padding bits past each row's last pixel are forced to zero on the way
 * in. The PBM spec calls them "should be zero", not "must"; the canvas
 * packing this tool feeds promises zero tails, so dirty tails get cleaned at
 * the door instead of leaking into every generated header.
 *
 * Ink polarity note: PBM renders bit 1 as black, and visor's Color::On is
 * the ink direction too (the chapter 6 export writes On as 1), so bits cross
 * unchanged in both directions.
 */
[[nodiscard]] std::expected<MonoImage, PbmError> parse_pbm(std::span<const std::uint8_t> bytes);

/**
 * @brief   Reads a whole file as raw bytes.
 * @note    A missing file and an unreadable file are reported as failures,
 *          not thrown and not terminated: input errors must not kill the tool.
 */
[[nodiscard]] std::expected<std::vector<std::uint8_t>, PbmError>
read_binary_file(std::string_view path);

} // namespace visor::fontgen
