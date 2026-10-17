// Compile tests of the shader override library (work/runtime/src/graphics/
// d3d12/shader_override.hlsli) and the look mods (work/fm4-rex/look) with FXC,
// as shader_override.cpp compiles them (ps_5_1, unbounded descriptor tables,
// IEEE strictness, /O3), without the game: the contract of a DXBC translation
// is synthetic here, a pixel shader with every fetch constant bound as 2D, 3D
// and cube, and every float constant. Each mod is compiled after the library
// and before a synthetic override that fetches and reads constants through the
// hooks, like a generated one.
//
// The library's code under #ifdef XE_POST_AO_VIEW is only compiled when the
// contract defines it, so both contracts are compiled, and the disassembly must
// show the occlusion texture read (resinfo, from xe_post_ao) exactly where it
// is expected: in an override that calls xe_post_ao with the define, never
// without it, never in a mod at its default knobs, and in at least one mod
// with FM4_LOOK_AO=1 and with FM4_LOOK_AO_DEBUG=1.
//
// usage: fm4_shader_mod_tests <shader_override.hlsli> <look directory>

#include <windows.h>

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

int failures = 0;

void Check(bool condition, const std::string& what) {
  std::printf("%s %s\n", condition ? "ok  " : "FAIL", what.c_str());
  if (!condition) ++failures;
}

bool ReadFile(const std::filesystem::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return true;
}

constexpr uint32_t kFetchConstants = 32;
constexpr uint32_t kInterpolators = 16;
// Any index past the system views: only compiled, never bound.
constexpr uint32_t kPostAoView = 3;

// What ShaderOverrides::BuildContract writes for a pixel shader with color
// target 0, all interpolators, every fetch constant bound as 2D, 3D and cube
// (unsigned and signed) with its default sampler, and every float constant.
std::string Contract(bool post_ao) {
  std::string out = "// Synthetic contract (fm4_shader_mod_tests).\n#define XE_PIXEL_SHADER 1\n";
  auto line = [&out](const std::string& text) { out += text + "\n"; };
  line("#define XE_SYSTEM_CONSTANT_VECTORS 16");
  line("#define XE_SYS_FLAGS 0u");
  line("#define XE_SYS_TEXTURE_SWIZZLED_SIGNS 16u");
  line("#define XE_SYS_TEXTURES_RESOLUTION_SCALED 48u");
  line("#define XE_SYS_SAMPLE_COUNT_LOG2 52u");
  line("#define XE_SYS_ALPHA_TEST_REFERENCE 60u");
  line("#define XE_SYS_ALPHA_TO_MASK 64u");
  line("#define XE_SYS_COLOR_EXP_BIAS 80u");
  line("#define XE_SYS_TEXTURE_INTEGER_SCALE_BITS 96u");
  line("#define XE_SYSFLAG_PRIMITIVE_POLYGONAL 2u");
  line("#define XE_SYSFLAG_PRIMITIVE_LINE_SHIFT 2u");
  line("#define XE_SYSFLAG_DEPTH_FLOAT24 8u");
  line("#define XE_SYSFLAG_ALPHA_PASS_IF_LESS_SHIFT 4u");
  line("#define XE_SYSFLAG_CONVERT_COLOR0_TO_GAMMA_SHIFT 7u");
  line("#define XE_RESOLUTION_SCALE_X 2");
  line("#define XE_RESOLUTION_SCALE_Y 2");
  line("#define XE_REVERT_RESOLUTION_SCALE_AXES 3u");
  line("#define XE_GAMMA_RENDER_TARGET_AS_UNORM8 1");
  line("#define XE_MSAA_2X_SUPPORTED 1");
  line("#define XE_FUZZY_ALPHA_EPSILON 0");
  if (post_ao) {
    line("#define XE_POST_AO_VIEW " + std::to_string(kPostAoView) + "u");
  }
  line("#define XE_REGISTER_COUNT " + std::to_string(kInterpolators));
  line("#define XE_INTERPOLATOR_MASK 0xFFFFu");
  line("#define XE_PARAM_GEN_ENABLED 0");
  line("#define XE_PARAM_GEN_INTERPOLATOR 0");
  line("#define XE_PARAM_GEN_POINT 0");
  line("#define XE_COLOR_TARGETS_WRITTEN 0x1");
  line("#define XE_WRITES_DEPTH 0");
  line("#define XE_FORCE_EARLY_DEPTH_STENCIL 0");
  line("#define XE_PS_ATTRIBUTES ");
  line("#define XE_FLOAT_CONSTANT_COUNT 256");
  for (uint32_t i = 0; i < 256; ++i) {
    line("#define XE_FLOAT_CONSTANT_" + std::to_string(i) + " xe_float_constants[" +
         std::to_string(i) + "]");
  }
  uint32_t slot = 0;
  std::string slots;
  for (uint32_t fetch = 0; fetch < kFetchConstants; ++fetch) {
    for (const char* dimension : {"2D", "3D", "CUBE"}) {
      for (char sign : {'U', 'S'}) {
        slots += "#define XE_TEXTURE" + std::to_string(fetch) + "_" + dimension + "_" + sign +
                 " " + std::to_string(slot++) + "u\n";
      }
    }
    slots += "#define XE_SAMPLER" + std::to_string(fetch) + "_FFF " + std::to_string(slot++) +
             "u\n";
  }
  line("#define XE_DESCRIPTOR_INDEX_VECTORS " + std::to_string((slot + 3) / 4));
  out += slots;
  line("struct XePsInput {");
  for (uint32_t i = 0; i < kInterpolators; ++i) {
    line("  float4 xe_interpolator" + std::to_string(i) + " : TEXCOORD" + std::to_string(i) +
         ";");
  }
  line("  float4 xe_position : SV_Position;");
  line("  bool xe_is_front_face : SV_IsFrontFace;");
  line("};");
  line("struct XePsOutput {");
  line("  float4 xe_color0 : SV_Target0;");
  line("  uint xe_coverage : SV_Coverage;");
  line("};");
  out += "#define XE_LOAD_INTERPOLATORS(p, input)";
  for (uint32_t i = 0; i < kInterpolators; ++i) {
    out += " p.r[" + std::to_string(i) + "] = input.xe_interpolator" + std::to_string(i) + ";";
  }
  out += "\n";
  return out;
}

// An override in the form HlslShaderTranslator generates: every texture fetch
// through XE_HOOK_TFETCH (2D, 3D and cube on the fetch constants the look mods
// hook) and constants through XE_CONSTANT.
const char kOverride[] = R"(void xe_pixel_shader(inout XePixel p) {
  float4 r0 = p.r[0], r1 = p.r[1], r2 = p.r[2], r3 = p.r[3];
  float4 sum = 0.0;
  {
    float4 t = XE_HOOK_TFETCH(0u, XE_TFETCH2D(0, r0.xy), r0);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(2u, XE_TFETCH2D(2, r0.zw), r0.zwxy);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(4u, XE_TFETCH2D(4, r1.xy), r1);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(13u, XE_TFETCH2D(13, r3.xy), r3);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(7u, XE_TFETCH3D(7, saturate(sum.xyz)), float4(saturate(sum.xyz), 0.0));
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(5u, XE_TFETCHCUBE(5, r2.xyz), r2);
    sum += t;
  }
  {
    float4 t = XE_HOOK_TFETCH(6u, XE_TFETCHCUBE(6, r2.zyx), r2.zyxw);
    sum += t;
  }
  for (uint i = 0u; i < 3u; ++i) {
    sum += xe_mul(sum, XE_CONSTANT(0));
  }
  sum += XE_CONSTANT(2) + XE_CONSTANT(4) + XE_CONSTANT(5) + XE_CONSTANT(8) + XE_CONSTANT(19) +
         XE_CONSTANT(22) + XE_CONSTANT(59) + XE_CONSTANT(255);
  p.color[0] = sum;
}
)";

// An override that only outputs the occlusion.
const char kPostAoOverride[] = R"(void xe_pixel_shader(inout XePixel p) {
  p.color[0] = xe_post_ao(p.r[0].xy).xxxx;
}
)";

// Serves "xenos_override.hlsli" (the library, as the runtime embeds it) and the
// files of the look directory (shader_mod_path).
class Include final : public ID3DInclude {
 public:
  Include(const std::string& library, const std::filesystem::path& mods)
      : library_(library), mods_(mods) {}
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data,
                         UINT* bytes) noexcept override {
    if (std::string(name) == "xenos_override.hlsli") {
      *data = library_.data();
      *bytes = UINT(library_.size());
      return S_OK;
    }
    std::string text;
    if (mods_.empty() || !ReadFile(mods_ / name, text)) {
      return E_FAIL;
    }
    std::string& file = files_.emplace_back(std::move(text));
    *data = file.data();
    *bytes = UINT(file.size());
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID data) noexcept override {
    files_.remove_if([data](const std::string& text) { return text.data() == data; });
    return S_OK;
  }

 private:
  const std::string& library_;
  std::filesystem::path mods_;
  std::list<std::string> files_;
};

struct Result {
  bool compiled = false;
  bool reads_post_ao = false;
  std::string messages;
};

// ShaderOverrides::Compile: contract, library, mod, override, main.
Result Compile(const std::string& library, const std::filesystem::path& mods,
               const std::string& mod, bool post_ao, const char* override_text,
               const std::vector<std::pair<std::string, std::string>>& defines) {
  std::string source = Contract(post_ao) + "#include \"xenos_override.hlsli\"\n";
  if (!mod.empty()) {
    source += "#include \"" + mod + "\"\n";
  }
  source += "#line 1 \"override.ps.hlsl\"\n";
  source += override_text;
  source += "\n#define XE_OVERRIDE_MAIN\n#include \"xenos_override.hlsli\"\n";
  std::vector<D3D_SHADER_MACRO> macros;
  for (const auto& define : defines) {
    macros.push_back({define.first.c_str(), define.second.c_str()});
  }
  macros.push_back({nullptr, nullptr});
  Include include(library, mods);
  ComPtr<ID3DBlob> code, errors;
  HRESULT hr = D3DCompile(source.data(), source.size(), "main.hlsl", macros.data(), &include,
                          "main", "ps_5_1",
                          D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES |
                              D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                          0, &code, &errors);
  Result result;
  if (errors) {
    result.messages.assign(static_cast<const char*>(errors->GetBufferPointer()),
                           errors->GetBufferSize());
  }
  if (FAILED(hr) || !code) {
    return result;
  }
  result.compiled = true;
  ComPtr<ID3DBlob> disassembly;
  if (SUCCEEDED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr,
                               &disassembly))) {
    std::string text(static_cast<const char*>(disassembly->GetBufferPointer()),
                     disassembly->GetBufferSize());
    // xe_post_ao is the library's only GetDimensions.
    result.reads_post_ao = text.find("resinfo") != std::string::npos;
  }
  return result;
}

void Report(const Result& result, const std::string& what) {
  Check(result.compiled, what + " compiles");
  if (!result.compiled || result.messages.find("warning") != std::string::npos) {
    std::printf("%s\n", result.messages.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: fm4_shader_mod_tests <shader_override.hlsli> <look directory>\n");
    return 2;
  }
  std::string library;
  if (!ReadFile(argv[1], library)) {
    std::printf("FAIL cannot read %s\n", argv[1]);
    return 1;
  }
  std::filesystem::path look = argv[2];

  // The library alone: the occlusion read exists with XE_POST_AO_VIEW only.
  for (bool post_ao : {true, false}) {
    std::string contract = post_ao ? "with XE_POST_AO_VIEW" : "without XE_POST_AO_VIEW";
    Result plain = Compile(library, {}, {}, post_ao, kOverride, {});
    Report(plain, "library, override with hooks, " + contract);
    Check(plain.compiled && !plain.reads_post_ao,
          "library, override with hooks, " + contract + ": no occlusion read");
    Result ao = Compile(library, {}, {}, post_ao, kPostAoOverride, {});
    Report(ao, "library, override calling xe_post_ao, " + contract);
    Check(ao.compiled && ao.reads_post_ao == post_ao,
          "library, override calling xe_post_ao, " + contract +
              (post_ao ? ": reads the occlusion" : ": returns 0 without a read"));
  }

  // Every look mod, at its default knobs and with each knob that reads the
  // occlusion.
  const char* const kAoKnobs[] = {"FM4_LOOK_AO", "FM4_LOOK_AO_DEBUG"};
  std::vector<std::filesystem::path> mods;
  for (const auto& entry : std::filesystem::directory_iterator(look)) {
    std::string name = entry.path().filename().string();
    if (name.starts_with("shader_") && name.ends_with(".mod.hlsli")) {
      mods.push_back(entry.path());
    }
  }
  std::sort(mods.begin(), mods.end());
  Check(!mods.empty(), "look directory has shader_<hash>.mod.hlsli files");
  int readers[std::size(kAoKnobs)] = {};
  for (const auto& path : mods) {
    std::string name = path.filename().string();
    for (bool post_ao : {true, false}) {
      std::string contract = post_ao ? "with XE_POST_AO_VIEW" : "without XE_POST_AO_VIEW";
      Result plain = Compile(library, look, name, post_ao, kOverride, {});
      Report(plain, name + ", default knobs, " + contract);
      Check(plain.compiled && !plain.reads_post_ao,
            name + ", default knobs, " + contract + ": no occlusion read");
      for (size_t k = 0; k < std::size(kAoKnobs); ++k) {
        std::string knob = std::string(kAoKnobs[k]) + "=1";
        Result result = Compile(library, look, name, post_ao, kOverride, {{kAoKnobs[k], "1.0"}});
        Report(result, name + ", " + knob + ", " + contract);
        if (post_ao) {
          std::printf("     %s, %s: %s\n", name.c_str(), knob.c_str(),
                      result.reads_post_ao ? "reads the occlusion" : "no occlusion read");
          readers[k] += result.reads_post_ao ? 1 : 0;
        } else {
          Check(!result.reads_post_ao, name + ", " + knob + ", " + contract + ": no occlusion read");
        }
      }
    }
  }
  for (size_t k = 0; k < std::size(kAoKnobs); ++k) {
    Check(readers[k] > 0, std::string("a look mod reads the occlusion with ") + kAoKnobs[k] +
                              "=1 (" + std::to_string(readers[k]) + ")");
  }

  std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
