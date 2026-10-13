#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_manager.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

void FM4MountCache(rex::Runtime* runtime) {
  // FM4's replay stream constructor expects cache:\\ to be writable. Its caller
  // dereferences the returned stream, so the SDK default of no cache device
  // fails here (guest 82930190 after the cache-root open fails).
  const auto directory = runtime->cache_root() / "fm4-game-cache";
  std::filesystem::create_directories(directory);
  constexpr auto device_path = "\\Device\\FM4Cache";
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(
      device_path, directory, false, true);
  auto* vfs = runtime->file_system();
  if (!device->Initialize() || !vfs->RegisterDevice(std::move(device)) ||
      !vfs->RegisterSymbolicLink("cache:", device_path))
    throw std::runtime_error("Unable to mount FM4 game cache");
  std::fprintf(stderr, "FM4 cache: mounted %s\n", directory.string().c_str());
}

// Explicit developer import from the user's extracted Disc 2. The runtime's
// installer preserves the package metadata and extracts into user_data_root.
void FM4InstallDisc2(rex::Runtime* runtime) {
  const char* source = std::getenv("FM4_DISC2_ROOT");
  if (!source || !*source) return;
  auto* kernel = runtime->kernel_state();
  if (kernel->title_id() != 0x4D530910)
    throw std::runtime_error("Disc 2 import requires FM4's loaded title ID");
  const auto packages = std::filesystem::path(source) / "content" /
      "0000000000000000" / "4d530910" / "00000002";
  auto* content = kernel->content_manager();
  for (unsigned index = 1; index <= 4; ++index) {
    char name[32];
    std::snprintf(name, sizeof(name), "4d5309100000000%u", index);
    const auto package = packages / name;
    const auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package);
    if (!header || uint32_t(header->metadata.execution_info.title_id) != 0x4D530910 ||
        header->metadata.content_type != rex::filesystem::XContentType::kMarketplaceContent)
      throw std::runtime_error("Invalid FM4 Disc 2 package metadata: " + package.string());
    const auto marker = runtime->user_data_root() / (std::string(name) + ".import-complete");
    const auto size = std::filesystem::file_size(package);
    uintmax_t previous_size = 0;
    { std::ifstream previous(marker); previous >> previous_size; }
    const auto destination = runtime->user_data_root() / "0000000000000000" /
        "4D530910" / "00000002" / name;
    if (previous_size == size && std::filesystem::is_directory(destination)) {
      std::fprintf(stderr, "FM4 Disc 2: %s already imported\n", name);
      continue;
    }
    std::fprintf(stderr, "FM4 Disc 2: importing %s (%llu bytes)\n", name,
                 static_cast<unsigned long long>(size));
    std::fflush(stderr);
    const auto result = content->InstallContent(package);
    if (result != 0)
      throw std::runtime_error("Disc 2 import failed: " + std::to_string(result));
    size_t files = 0;
    uintmax_t bytes = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(destination)) {
      if (entry.is_regular_file()) { ++files; bytes += entry.file_size(); }
    }
    if (!files || !bytes) throw std::runtime_error("Disc 2 import produced no files");
    std::ofstream record(marker);
    record << size << '\n' << files << '\n' << bytes << '\n';
    record.flush();
    if (!record) throw std::runtime_error("Unable to record Disc 2 import completion");
    std::fprintf(stderr, "FM4 Disc 2: imported %s files=%zu bytes=%llu\n", name, files,
                 static_cast<unsigned long long>(bytes));
    std::fflush(stderr);
  }
  // Developer import mode stops before the guest entry point is launched.
  if (std::getenv("FM4_DISC2_INSTALL_ONLY")) {
    std::fprintf(stderr, "FM4 Disc 2: all four content packages installed\n");
    std::fflush(stderr);
    std::exit(0);
  }
}
