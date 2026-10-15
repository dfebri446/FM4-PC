/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Compute shader to downscale scaled resolve buffer data back to 1x resolution.
// Operates on 32x32 tiled data format used by Xbox 360.
// Each thread handles one output pixel (one 32x32 tile = 1024 threads).
//
// FM4 adaptation: the scaled data is laid out as the ReXGlue texture loads and
// resolves (and the FM4 full resolves, XeniaTextureResolutionScaledGroupBlocksLog2)
// address it: in groups of one row of 8 bytes for 8bpp and 16 bytes otherwise.
// The guest group at guest offset G is stored at G * scale_x * scale_y, as
// scale_x * scale_y host groups in column-major order (host group column *
// scale_y + row), and guest block b of the group covers host blocks
// b * scale_x ... (b + 1) * scale_x - 1 of each of its scale_y rows. The
// original code assumed scale_x * scale_y consecutive host pixels per guest
// pixel, which shuffled the pixels of every scaled readback.
//
// By default, picks the top-left pixel of each scale_x * scale_y block.
// When xe_downscale_half_pixel_offset is set, samples from (scale/2, scale/2)
// within each block to compensate for the half-pixel offset becoming a
// full-pixel offset at higher resolutions.

cbuffer XeResolveDownscaleConstants : register(b0) {
  uint xe_downscale_scale_x;         // 1 to kMaxDrawResolutionScaleAlongAxis
  uint xe_downscale_scale_y;         // 1 to kMaxDrawResolutionScaleAlongAxis
  uint xe_downscale_pixel_size_log2; // 0=8bit, 1=16bit, 2=32bit, 3=64bit
  uint xe_downscale_tile_count;      // Number of 32x32 tiles to process
  // When non-zero, apply half-pixel offset correction by sampling from
  // (scale/2, scale/2) within each scaled block instead of (0, 0).
  // This compensates for the D3D9-style half-pixel offset used by Xbox 360
  // games, which at Nx resolution scaling shifts rendered content by
  // (N/2, N/2) host pixels.
  uint xe_downscale_half_pixel_offset;
};

ByteAddressBuffer xe_resolve_source : register(t0);
RWByteAddressBuffer xe_resolve_dest : register(u0);

// Groupshared memory for coalescing sub-32-bit writes
// Max tile size at 1x is 32*32*8 bytes = 8KB for 64-bit pixels
// We only need this for 8-bit and 16-bit where we coalesce to 32-bit stores
groupshared uint gs_tile_data[32 * 32];

// Thread group processes one 32x32 tile worth of output
[numthreads(32, 32, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_thread_id_in_group : SV_GroupThreadID,
          uint xe_group_thread_index : SV_GroupIndex) {
  uint tile_index = xe_group_id.x;

  // Early out if beyond tile count
  [branch] if (tile_index >= xe_downscale_tile_count) {
    return;
  }

  uint row = xe_thread_id_in_group.y;
  uint column = xe_thread_id_in_group.x;
  uint pixel_index = row * 32 + column;  // 0-1023

  uint pixel_size = 1u << xe_downscale_pixel_size_log2;
  uint tile_size_1x = 32 * 32 * pixel_size;
  uint scale_xy = xe_downscale_scale_x * xe_downscale_scale_y;

  // Compute the host sample within each scaled guest pixel to sample from.
  // Without half-pixel correction: sample from (0, 0).
  // With half-pixel correction: sample from (scale/2, scale/2) to compensate
  // for the D3D9-style half-pixel offset shifting content by (N/2, N/2) pixels
  // at Nx resolution.
  uint offset_x = 0u;
  uint offset_y = 0u;
  [branch] if (xe_downscale_half_pixel_offset != 0u && scale_xy > 1u) {
    offset_x = xe_downscale_scale_x >> 1u;
    offset_y = xe_downscale_scale_y >> 1u;
  }

  // Destination offset in 1x buffer
  uint dst_offset = tile_index * tile_size_1x +
                    pixel_index * pixel_size;

  // Source offset: the guest group scaled, the host group within it, and the
  // host block within that (see the layout above).
  uint group_bytes_log2 = xe_downscale_pixel_size_log2 == 0u ? 3u : 4u;
  uint group_blocks_log2 = group_bytes_log2 - xe_downscale_pixel_size_log2;
  uint group_blocks_mask = (1u << group_blocks_log2) - 1u;
  uint guest_group_offset = dst_offset & ~((1u << group_bytes_log2) - 1u);
  uint block_in_group = (dst_offset >> xe_downscale_pixel_size_log2) & group_blocks_mask;
  uint host_block_x = block_in_group * xe_downscale_scale_x + offset_x;
  uint host_group_index =
      (host_block_x >> group_blocks_log2) * xe_downscale_scale_y + offset_y;
  uint src_offset = guest_group_offset * scale_xy +
                    (host_group_index << group_bytes_log2) +
                    ((host_block_x & group_blocks_mask) << xe_downscale_pixel_size_log2);

  // Copy pixel based on size
  [branch] switch (xe_downscale_pixel_size_log2) {
    case 0: {  // 8-bit - use groupshared to coalesce 4 bytes into 32-bit writes
      // Load the byte value
      uint byte_val = (xe_resolve_source.Load(src_offset & ~3u) >>
                       ((src_offset & 3u) * 8u)) & 0xFFu;

      // Each group of 4 threads packs into one uint
      uint pack_index = pixel_index >> 2;   // Which uint (0-255)
      uint byte_pos = pixel_index & 3u;     // Which byte in uint (0-3)

      // Pack byte into shared memory uint using atomics within group
      uint contribution = byte_val << (byte_pos * 8u);
      if (byte_pos == 0u) {
        gs_tile_data[pack_index] = contribution;
      }
      GroupMemoryBarrierWithGroupSync();
      if (byte_pos != 0u) {
        InterlockedOr(gs_tile_data[pack_index], contribution);
      }
      GroupMemoryBarrierWithGroupSync();

      // First thread of each 4 writes the packed uint
      if (byte_pos == 0u) {
        xe_resolve_dest.Store(tile_index * (32 * 32) + pack_index * 4,
                              gs_tile_data[pack_index]);
      }
      break;
    }
    case 1: {  // 16-bit - use groupshared to coalesce 2 shorts into 32-bit writes
      // Load the short value
      uint short_val = (xe_resolve_source.Load(src_offset & ~3u) >>
                        ((src_offset & 2u) * 8u)) & 0xFFFFu;

      // Each group of 2 threads packs into one uint
      uint pack_index = pixel_index >> 1;   // Which uint (0-511)
      uint short_pos = pixel_index & 1u;    // Which short in uint (0-1)

      // Pack short into shared memory
      uint contribution = short_val << (short_pos * 16u);
      if (short_pos == 0u) {
        gs_tile_data[pack_index] = contribution;
      }
      GroupMemoryBarrierWithGroupSync();
      if (short_pos != 0u) {
        InterlockedOr(gs_tile_data[pack_index], contribution);
      }
      GroupMemoryBarrierWithGroupSync();

      // First thread of each 2 writes the packed uint
      if (short_pos == 0u) {
        xe_resolve_dest.Store(tile_index * (32 * 32 * 2) + pack_index * 4,
                              gs_tile_data[pack_index]);
      }
      break;
    }
    case 2: {  // 32-bit - direct copy
      xe_resolve_dest.Store(dst_offset, xe_resolve_source.Load(src_offset));
      break;
    }
    case 3: {  // 64-bit - direct copy
      xe_resolve_dest.Store2(dst_offset, xe_resolve_source.Load2(src_offset));
      break;
    }
  }
}
