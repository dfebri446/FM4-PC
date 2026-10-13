// SDK loader adapter. Reads the user's XEX; never starts guest execution.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/system/user_module.h>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  rex::InitLogging();
  const auto input = std::filesystem::absolute(argv[1]);
  rex::Runtime runtime(input.parent_path().string());
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  if (runtime.Setup(std::move(config)) != 0) return 3;
  if (runtime.LoadXexImage("game:\\" + input.filename().string()) != 0) return 4;
  const auto user = runtime.kernel_state()->GetExecutableModule();
  if (!user) return 5;
  const auto* module = user->xex_module();
  if (!module || module->base_address() != 0x82000000) return 6;
  std::vector<char> image(module->image_size(), 0);
  for (const auto& section : module->binary_sections()) {
    const auto offset = uint64_t(section.virtual_address) - module->base_address();
    // Match the SDK codegen BinaryView: unmapped/out-of-image sections are skipped.
    if (!section.host_data || offset + section.virtual_size > image.size()) continue;
    std::memcpy(image.data() + offset, section.host_data, section.virtual_size);
  }
  std::ofstream out(argv[2], std::ios::binary);
  out.write(image.data(), image.size());
  return out ? 0 : 8;
}
