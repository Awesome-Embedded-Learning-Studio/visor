#include "font.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

namespace visor::fontgen {

const char* describe(CutError error) noexcept {
    switch (error) {
        case CutError::kBadCell:
            return "cell width and height must be at least 1";
        case CutError::kSheetTooSmall:
            return "the sheet holds fewer pixels than one cell";
        case CutError::kNotDivisible:
            return "sheet width or height is not a whole multiple of the cell";
    }
    return "unknown cut error";
}

std::expected<std::vector<Glyph>, CutError> cut_glyph_sheet(const MonoImage& sheet,
                                                            std::size_t cell_width,
                                                            std::size_t cell_height,
                                                            std::uint32_t first_codepoint) {
    if (cell_width == 0 || cell_height == 0) {
        return std::unexpected{CutError::kBadCell};
    }
    if (sheet.width < cell_width || sheet.height < cell_height) {
        return std::unexpected{CutError::kSheetTooSmall};
    }
    if (sheet.width % cell_width != 0 || sheet.height % cell_height != 0) {
        return std::unexpected{CutError::kNotDivisible};
    }

    const std::size_t columns = sheet.width / cell_width;
    const std::size_t cell_rows = sheet.height / cell_height;
    const std::size_t sheet_stride = sheet.stride();
    std::vector<Glyph> glyphs;
    glyphs.reserve(columns * cell_rows);
    for (std::size_t cell_row = 0; cell_row < cell_rows; ++cell_row) {
        for (std::size_t column = 0; column < columns; ++column) {
            Glyph glyph;
            glyph.codepoint =
                first_codepoint + static_cast<std::uint32_t>((cell_row * columns) + column);
            glyph.image = make_mono_image(cell_width, cell_height);
            glyph.advance = cell_width; // strip metrics: the gap lives in the cells
            for (std::size_t y = 0; y < cell_height; ++y) {
                const std::size_t sheet_row = (cell_row * cell_height) + y;
                for (std::size_t x = 0; x < cell_width; ++x) {
                    // Bit-level addressing in the raw, the same boundary call
                    // the PBM parser makes: this tool stays independent of
                    // visor::base, five shifts buy that boundary (B7).
                    const std::size_t sheet_x = (column * cell_width) + x;
                    const std::uint8_t source =
                        sheet.rows[(sheet_row * sheet_stride) + (sheet_x / 8)];
                    if ((source & static_cast<std::uint8_t>(0x80U >> (sheet_x % 8))) != 0) {
                        glyph.image.rows[(y * glyph.image.stride()) + (x / 8)] |=
                            static_cast<std::uint8_t>(0x80U >> (x % 8));
                    }
                }
            }
            glyphs.push_back(std::move(glyph));
        }
    }
    return glyphs;
}

} // namespace visor::fontgen
