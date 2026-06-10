#pragma once

#include "../../SpiceCore/Binary/Endian.h"

namespace Sa3Dport::Structs {

using Endian = ::spice::core::Endian;
using ::spice::core::byteswap;
using ::spice::core::byteswap_float;
using ::spice::core::needs_swap;

} // namespace Sa3Dport::Structs
