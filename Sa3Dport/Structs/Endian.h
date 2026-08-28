#pragma once

#include "../../SpiceRoot/Binary/Endian.h"

namespace Sa3Dport::Structs {

using Endian = ::spice::root::Endian;
using ::spice::root::byteswap;
using ::spice::root::byteswap_float;
using ::spice::root::needs_swap;

} // namespace Sa3Dport::Structs
