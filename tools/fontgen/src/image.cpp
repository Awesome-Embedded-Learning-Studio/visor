#include "image.hpp"

namespace visor::fontgen {

MonoImage make_mono_image(std::size_t width, std::size_t height) {
    MonoImage image;
    image.width = width;
    image.height = height;
    image.rows.resize(height * image.stride());
    return image;
}

} // namespace visor::fontgen
