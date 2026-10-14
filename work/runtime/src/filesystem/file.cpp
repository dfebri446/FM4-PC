#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>

namespace rex::filesystem {

File::File(uint32_t file_access, Entry* entry) : file_access_(file_access), entry_(entry) {
  entry_->RetainFile();
}

File::~File() {
  // Derived members (including the host handle) have already been destroyed.
  entry_->ReleaseFile();
}

}  // namespace rex::filesystem
