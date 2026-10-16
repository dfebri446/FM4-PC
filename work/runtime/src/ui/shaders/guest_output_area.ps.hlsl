// Guest output painting that averages over each output pixel's footprint when
// the guest output is shrunk (FM4 PC): with a render scale above the window
// size, for example 2x (2560x1440) on a 1920x1080 screen, every screen pixel
// is the mean of the source texels it covers, in linear light (supersampling),
// where the bilinear shader takes one tap and skips texels. Up to 1:1 it is the
// bilinear shader. Same bindings as guest_output_bilinear: the D3D12 presenter
// draws its bilinear effect with it (present_area_downscale).

cbuffer push_consts_xe : register(b0) {
  int2 xe_bilinear_output_offset;
  float2 xe_bilinear_output_size_inv;
};
Texture2D<float4> xe_bilinear_source : register(t0);
SamplerState xe_bilinear_sampler : register(s0);

float4 main(float4 xe_position : SV_Position) : SV_Target {
  float2 uv = (float2(int2(xe_position.xy) - xe_bilinear_output_offset) + 0.5) *
              xe_bilinear_output_size_inv;
  uint width, height;
  xe_bilinear_source.GetDimensions(width, height);
  // Source texels per output pixel along each axis.
  float2 footprint = float2(width, height) * xe_bilinear_output_size_inv;
  if (all(footprint <= 1.0)) {
    return float4(xe_bilinear_source.SampleLevel(xe_bilinear_sampler, uv, 0.0).rgb, 1.0);
  }
  // Bilinear taps at most one texel apart across the footprint (each the mean of
  // up to 2x2 texels; at 2:1 exactly the texel centers): a box over the
  // footprint. Averaged as gamma 2.2 linear light, so thin bright and dark
  // details keep their share, as in a multisample resolve.
  uint2 taps = min(uint2(ceil(footprint)), uint2(8u, 8u));
  float2 span = footprint / float2(width, height);
  float2 spacing = span / float2(taps);
  float2 first = uv - 0.5 * span + 0.5 * spacing;
  float3 sum = 0.0;
  for (uint y = 0u; y < taps.y; ++y) {
    for (uint x = 0u; x < taps.x; ++x) {
      float3 color =
          xe_bilinear_source.SampleLevel(xe_bilinear_sampler, first + float2(x, y) * spacing, 0.0)
              .rgb;
      sum += pow(saturate(color), 2.2);
    }
  }
  return float4(pow(max(sum / float(taps.x * taps.y), 0.0), 1.0 / 2.2), 1.0);
}
