#ifndef VOXEL_UTILS_H
#define VOXEL_UTILS_H

#include "../Math/Vector2.h"

// Aliases for various coordinate systems. All of these are from a top-down perspective, like a 2D array.
using OriginalInt2 = Int2; // +X west, +Y south (original game, origin at top right).
using NewInt2 = Int2; // +X south, +Y west (DEPRECATE THIS EVENTUALLY IN FAVOR OF CHUNKS).
using ChunkInt2 = Int2; // +X east, +Y south, [-inf, inf] (like a C array, origin at top left).
using ChunkVoxelInt2 = Int2; // Same directions as chunk, [0, CHUNK_DIM-1].
using AbsoluteChunkVoxelInt2 = Int2; // Chunk voxel multiplied by chunk coordinates, [-inf, inf].

using OriginalDouble2 = Double2; // +X west, +Y south.
using NewDouble2 = Double2; // +X south, +Y west.

struct ChunkCoord
{
	ChunkInt2 chunk;
	ChunkVoxelInt2 voxel;
};

// These are here out of desperation after many months of confusing myself.
using NSInt = int; // + north, - south
using SNInt = int; // + south, - north
using EWInt = int; // + east, - west
using WEInt = int; // + west, - east

namespace VoxelUtils
{
	const NewInt2 North(-1, 0);
	const NewInt2 South(1, 0);
	const NewInt2 East(0, -1);
	const NewInt2 West(0, 1);

	// Transformation methods for converting voxel coordinates between the original game's format
	// (+X west, +Z south) and the new format (+X south, +Z west). This is a bi-directional
	// conversion (i.e., it works both ways).
	NewInt2 originalVoxelToNewVoxel(const OriginalInt2 &voxel);
	OriginalInt2 newVoxelToOriginalVoxel(const NewInt2 &voxel);
	Double2 getTransformedVoxel(const Double2 &voxel);

	// Converts a voxel from chunk space to new voxel grid space.
	NewInt2 chunkVoxelToNewVoxel(const ChunkInt2 &chunk, const ChunkVoxelInt2 &voxel,
		SNInt gridWidth, WEInt gridDepth);

	// Converts a voxel from chunk space to absolute chunk voxel space.
	AbsoluteChunkVoxelInt2 chunkVoxelToAbsoluteChunkVoxel(const ChunkInt2 &chunk,
		const ChunkVoxelInt2 &voxel);

	// Converts a voxel from new voxel grid space to chunk voxel space.
	ChunkCoord newVoxelToChunkVoxel(const NewInt2 &voxel, SNInt gridWidth, WEInt gridDepth);

	// Gets the chunk that a new voxel would be in.
	ChunkInt2 newVoxelToChunk(const NewInt2 &voxel, SNInt gridWidth, WEInt gridDepth);

	// Converts a voxel from new voxel space to absolute chunk voxel space.
	AbsoluteChunkVoxelInt2 newVoxelToAbsoluteChunkVoxel(const NewInt2 &voxel,
		SNInt gridWidth, WEInt gridDepth);

	// Converts a voxel from absolute chunk voxel space to chunk voxel space.
	ChunkCoord absoluteChunkVoxelToChunkVoxel(const AbsoluteChunkVoxelInt2 &voxel);
}

#endif
