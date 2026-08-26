#include "PvmTextureModel.h"

namespace spice::pvm::model {

PixelFormat pixelFormatFromRaw(const std::uint8_t raw) noexcept
{
    switch (raw) {
    case 0x00: return PixelFormat::Argb1555;
    case 0x01: return PixelFormat::Rgb565;
    case 0x02: return PixelFormat::Argb4444;
    default: return PixelFormat::Unknown;
    }
}

DataLayout dataLayoutFromRaw(const std::uint8_t raw) noexcept
{
    switch (raw) {
    case 0x01: return DataLayout::Twiddled;
    case 0x02: return DataLayout::TwiddledMipmaps;
    case 0x03: return DataLayout::Vq;
    case 0x04: return DataLayout::VqMipmaps;
    case 0x09: return DataLayout::Rectangle;
    case 0x10: return DataLayout::SmallVq;
    case 0x11: return DataLayout::SmallVqMipmaps;
    case 0x12: return DataLayout::TwiddledMipmapsDma;
    default: return DataLayout::Unknown;
    }
}

} // namespace spice::pvm::model
