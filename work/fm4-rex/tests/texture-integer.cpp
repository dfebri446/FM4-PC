#include <rex/graphics/pipeline/texture/info.h>
#include <cstdio>
#include <cstdlib>
#include <array>
using namespace rex::graphics;
using F = xenos::TextureFormat;
static int checks = 0;
static uint32_t Swizzle(int r,int g,int b,int a) {return r|(g<<3)|(b<<6)|(a<<9);}
static void Expect(F format, bool integer, uint32_t swizzle, uint8_t signs,
                   std::array<uint32_t,4> scales) {
  uint32_t bits=FormatInfo::Get(format)->GetIntegerScaleBits(integer,swizzle,signs);
  for (int i=0;i<4;++i) {
    uint32_t lane=(bits>>(5*i))&31;
    uint32_t scale=(1u<<((lane&15)+1-(lane>>4)))-1;
    ++checks;
    if (scale!=scales[i]) {
      std::fprintf(stderr,"format %u component %d: %u != %u\n",unsigned(format),i,scale,scales[i]);
      std::exit(1);
    }
  }
}
int main() {
  auto rgba=Swizzle(0,1,2,3);
  for(unsigned f=0;f<64;++f) Expect(F(f),false,rgba,0x55,{1,1,1,1});
  Expect(F::k_16_16,true,Swizzle(0,1,4,5),0x55,{32767,32767,1,1});
  Expect(F::k_16_16,true,Swizzle(1,0,0,1),0x55,{32767,32767,32767,32767});
  Expect(F::k_16_16,true,rgba,0,{65535,65535,1,1});
  Expect(F::k_8_8_8_8,true,rgba,0,{255,255,255,255});
  Expect(F::k_8_8_8_8,true,rgba,0x55,{127,127,127,127});
  Expect(F::k_8_8_8_8,true,rgba,0xFF,{1,1,1,1});
  Expect(F::k_8_8_8_8,true,rgba,0xAA,{255,255,255,255});
  Expect(F::k_2_10_10_10,true,rgba,0,{1023,1023,1023,3});
  Expect(F::k_2_10_10_10,true,Swizzle(3,2,1,0),0x55,{1,511,511,511});
  Expect(F::k_5_6_5,true,Swizzle(2,1,0,5),0,{31,63,31,1});
  Expect(F::k_16_FLOAT,true,rgba,0x55,{1,1,1,1});
  Expect(F::k_16_16_FLOAT,true,rgba,0x55,{1,1,1,1});
  Expect(F::k_DXT1,true,rgba,0,{1,1,1,1});
  Expect(F::k_8_8_8_8,true,Swizzle(4,5,4,5),0x55,{1,1,1,1});
  std::printf("{\"checks\":%d,\"failures\":0}\n",checks);
}
