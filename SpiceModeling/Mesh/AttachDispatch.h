#pragma once

#include "Mesh/Attach.h"
#include "Mesh/Chunk/ChunkAttach.h"

namespace spice::modeling::Mesh {

inline AttachPtr Attach::read(const spice::modeling::Structs::EndianStackReader& reader,
                              std::uint32_t address,
                              spice::modeling::ObjectData::Enums::ModelFormat format,
                              AttachReadContext& context) {
    switch (format) {
    case spice::modeling::ObjectData::Enums::ModelFormat::SA2:
        return Chunk::ChunkAttach::read(reader, address, context);
    case spice::modeling::ObjectData::Enums::ModelFormat::SA1:
    case spice::modeling::ObjectData::Enums::ModelFormat::SADX:
    case spice::modeling::ObjectData::Enums::ModelFormat::SA2B:
    case spice::modeling::ObjectData::Enums::ModelFormat::Buffer:
    default:
        throw std::invalid_argument("attach format not implemented for this slice");
    }
}

} // namespace spice::modeling::Mesh
