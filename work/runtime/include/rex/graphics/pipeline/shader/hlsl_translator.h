/**
 * @file        graphics/pipeline/shader/hlsl_translator.h
 * @brief       Guest pixel shader ucode to HLSL shader overrides
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rex/graphics/pipeline/shader/dxbc.h>
#include <rex/graphics/pipeline/shader/translator.h>

namespace rex::graphics {

// Generates the HLSL of a shader override (void xe_pixel_shader(inout XePixel
// p), see d3d12/shader_override.hlsli) doing the same operations as the
// DxbcShaderTranslator translation of the same pixel shader and modification
// for the bindless host render target path, as a starting point for editing.
// Translate after the DXBC translation of the shader: the descriptor slots are
// named after that translation's bindings. The translated binary is the HLSL
// text. Vertex fetch, getWeights, getCompTexLOD, getBCF and memexport are
// fatal errors (the DXBC translation then stays).
class HlslShaderTranslator : public ShaderTranslator {
 public:
  HlslShaderTranslator() = default;

  // A scratch translation of a shader, not registered in the shader.
  class Translation : public Shader::Translation {
   public:
    Translation(Shader& shader, uint64_t modification)
        : Shader::Translation(shader, modification) {}
  };

  // Names of the descriptor index slots in the contract of a translation.
  static std::string TextureSlotName(uint32_t fetch_constant, xenos::FetchOpDimension dimension,
                                     bool is_signed);
  static std::string SamplerSlotName(const DxbcShader::SamplerBinding& binding);

 protected:
  void Reset() override;
  uint32_t GetModificationRegisterCount() const override;
  void StartTranslation() override;
  std::vector<uint8_t> CompleteTranslation() override;

  void ProcessLabel(uint32_t cf_index) override;
  void ProcessExecInstructionBegin(const ParsedExecInstruction& instr) override;
  void ProcessExecInstructionEnd(const ParsedExecInstruction& instr) override;
  void ProcessLoopStartInstruction(const ParsedLoopStartInstruction& instr) override;
  void ProcessLoopEndInstruction(const ParsedLoopEndInstruction& instr) override;
  void ProcessJumpInstruction(const ParsedJumpInstruction& instr) override;
  void ProcessAllocInstruction(const ParsedAllocInstruction& instr, uint8_t export_eM) override;
  void ProcessVertexFetchInstruction(const ParsedVertexFetchInstruction& instr) override;
  void ProcessTextureFetchInstruction(const ParsedTextureFetchInstruction& instr) override;
  void ProcessAluInstruction(const ParsedAluInstruction& instr,
                             uint8_t memexport_eM_potentially_written_before) override;

 private:
  // An operand as DxbcShaderTranslator::LoadOperand loads it: the swizzle of
  // components not needed repeats the first needed one.
  struct Operand {
    std::string base;  // Without swizzle and modifiers, or a literal 0.
    char swizzle[4] = {'x', 'x', 'x', 'x'};  // Underlying component per loaded one.
    bool absolute = false;
    bool negated = false;
    // The loaded float4.
    std::string Vector() const;
    // A component of the loaded float4 (SelectFromSwizzled).
    std::string Component(uint32_t index) const;
    // A component of the underlying register (Select).
    std::string Underlying(char component) const;
  };
  Operand LoadOperand(const InstructionOperand& operand, uint32_t needed_components) const;
  std::string RelativeIndex(InstructionStorageAddressingMode mode, uint32_t index) const;
  // Writes a float4 expression to the result (StoreResult).
  void StoreResult(const InstructionResult& result, const std::string& value);
  void EmitVectorOperation(const ParsedAluInstruction& instr);
  void EmitScalarOperation(const ParsedAluInstruction& instr);
  void EmitDisassembly(const std::string& disassembly);

  void Line(const std::string& text);
  void Open(const std::string& text);
  void Close();
  void CloseExec();

  std::string out_;
  uint32_t indent_ = 0;
  bool use_pc_loop_ = false;
  bool exec_open_ = false;
  bool label_open_ = false;
};

}  // namespace rex::graphics
