#include "SctScptDecodeHelpers.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace spice::sct::detail {

std::string toHexWord(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

float floatFromWordBits(std::uint32_t value) {
    float floatValue = 0.0f;
    std::memcpy(&floatValue, &value, sizeof(float));
    return floatValue;
}

} // namespace spice::sct::detail
