# FM4-PC

Proyek eksperimental recompilation **Forza Motorsport 4 untuk Windows x64**, menggunakan ReXGlue SDK dan patch runtime/GPU berbasis ReXGlue/Xenia.

Proyek ini menyediakan kode integrasi serta alur setup, generate, build, dan menjalankan game dari **data game milik pengguna**. ReXGlue menerjemahkan kode executable Xbox 360 menjadi C++ secara lokal, lalu toolchain membangunnya menjadi executable dan DLL untuk PC. Layanan Xbox dan grafis tetap ditangani oleh runtime SDK.

**Status: sudah dapat dibangun dan menjalankan intro game dari source bersih, tetapi belum merupakan full port dengan seluruh fitur dan stabilitas terverifikasi.** Beberapa balapan dan fitur lain berhasil diuji pada build pengembangan sebelumnya; cakupan pengujiannya dijelaskan di bawah.

Repo ini tidak menyertakan game, aset asli, kode hasil recompilation, atau binary siap main. Clone saja belum cukup: siapkan data game dan prasyarat build terlebih dahulu.

## Versi game yang didukung

- **Forza Motorsport 4 Japan/Asia**, Play Disc dengan media ID `633772A0`.
- Ketiga executable diperiksa menggunakan SHA-256 dalam [pipeline.py](outputs/FM4-build/pipeline.py). Region/revisi lain, executable modifikasi, dan title update yang tidak cocok akan ditolak.
- **Disc 1 wajib**, sudah diekstrak dan berisi seluruh aset game.
- **Disc 2 opsional** untuk startup game dasar, tetapi diperlukan untuk mengimpor konten mobil tambahannya.
- Ekstraksi ISO dilakukan sendiri. Skrip tidak mengunduh data game.

Contoh susunan direktori input:

```text
D:\Games\FM4\
├── Disc 1\
│   ├── default.xex
│   ├── XMediaFacade_default.xex
│   ├── SpeechFacade_default.xex
│   ├── Database.xmplr
│   ├── Media\
│   └── ... seluruh berkas Disc 1 lainnya
└── Disc 2\
    └── content\0000000000000000\4d530910\00000002\
        ├── 4d53091000000001
        ├── 4d53091000000002
        ├── 4d53091000000003
        └── 4d53091000000004
```

## Prasyarat

| Komponen | Kebutuhan |
| --- | --- |
| Sistem | Windows x64, CPU kompatibel x86-64-v2, GPU yang mendukung Direct3D 12 |
| Git | Git for Windows dan akses HTTPS ke GitHub untuk SDK/dependensi |
| Python | Python **3.11+ x64**, gunakan executable Python sebenarnya, bukan alias Microsoft Store |
| Visual Studio | Visual Studio 2022 atau Build Tools, workload **Desktop development with C++**, MSVC x64 |
| Windows SDK | Termasuk `fxc.exe`; versi yang diuji: **10.0.26100.0** |
| Build tools | CMake **3.25+** dan Ninja; dapat dipasang melalui Visual Studio Installer |
| LLVM | LLVM **20.x x64**; versi yang diuji: **20.1.8** |

Sediakan ruang disk dan waktu untuk source SDK, dependensi, hasil generate, kompilasi, serta impor Disc 2. Direktori build boleh ditempatkan di drive lain.

Skrip memeriksa prasyarat dan menyiapkan SDK proyek. Skrip **tidak otomatis memasang** Visual Studio, Python, atau LLVM, dan tidak mengubah pengaturan keamanan Windows.

## Setup dan build

Jalankan perintah berikut melalui PowerShell. Ganti seluruh path contoh sesuai komputer dan lokasi data gamemu.

### 1. Clone repo

```powershell
git clone https://github.com/dfebri446/FM4-PC.git
Set-Location FM4-PC
```

### 2. Tentukan direktori dan periksa toolchain

```powershell
$common = @{
    BuildRoot = 'D:\FM4-build'
    LLVMRoot  = 'C:\Program Files\LLVM'
    Python    = 'C:\Python311\python.exe'
    Jobs      = 4
}

.\outputs\FM4-build\Build.ps1 -Action doctor @common
```

`Build.ps1` menyiapkan environment Visual Studio x64 secara otomatis. `LLVMRoot` adalah direktori instalasi LLVM yang memiliki folder `bin`.

### 3. Setup, generate, dan build

```powershell
$discs = @{
    Disc1 = 'D:\Games\FM4\Disc 1'
    Disc2 = 'D:\Games\FM4\Disc 2'
}

.\outputs\FM4-build\Build.ps1 -Action all @common @discs
```

Hapus entri `Disc2` jika hanya ingin menggunakan konten Disc 1. Aksi `all` menjalankan tiga tahap:

1. **Setup:** mengambil SDK pada commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, menyiapkan submodule, menerapkan patch SDK, membangun shader resolve berlisensi, lalu membangun dan memasang SDK secara lokal.
2. **Generate:** memvalidasi executable milikmu, membaca image tanpa menjalankan kode guest, membentuk kembali tabel switch, dan menghasilkan C++ untuk executable utama serta kedua modul tambahan. Pemeriksaan hasil generate menolak diagnostik branch/label yang belum terselesaikan.
3. **Build:** mengompilasi executable/DLL, menyalin runtime dan plugin GPU yang cocok, menjalankan pemeriksaan instruksi, dan menyimpan `build-result.json`.

`all` tidak langsung membuka game. Hasil generate dan seluruh binary berada di `BuildRoot`, bukan di source yang masuk Git.

Tahap-tahap tersebut juga dapat dijalankan terpisah:

```powershell
.\outputs\FM4-build\Build.ps1 -Action setup @common
.\outputs\FM4-build\Build.ps1 -Action generate @common @discs
.\outputs\FM4-build\Build.ps1 -Action build @common
```

Gunakan `BuildRoot` yang sama untuk seluruh tahap. Setelah mengubah source integrasi, ulangi `generate` dan `build` agar salinan proyek lokal diperbarui. Jika revisi atau patch SDK berubah, gunakan `BuildRoot` baru.

## Menjalankan game

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common
```

Jika membuka PowerShell baru, definisikan kembali `$common` seperti contoh sebelumnya. Aksi `run` membaca lokasi disc yang disimpan saat `generate`.

Launcher memeriksa artifact build dan versi executable, menggunakan profil lokal baru, serta mengimpor empat paket Disc 2 jika path-nya diberikan. Impor pertama memerlukan waktu dan ruang tambahan. Shader sinkron dan workaround bayangan saat ini aktif secara bawaan.

Controller menggunakan backend SDL. Pemetaan keyboard dari launcher:

| Tombol | Fungsi |
| --- | --- |
| `W` / `S` | Gas / rem |
| `A` / `D` | Kemudi kiri / kanan |
| `Space` / `Backspace` | Tombol controller A / B |
| `Enter` / `Tab` | Start / Back |
| Tombol panah | D-pad |

Data lokal disimpan di bawah `BuildRoot`:

```text
FM4-build/
├── sdk-source/       # SDK dan dependensi yang diunduh
├── sdk-build/        # Build SDK
├── sdk-install/      # SDK yang dipasang lokal
├── project/          # Source integrasi dan generated/ privat
├── game-build/       # Executable, modul, runtime, plugin GPU
├── user-data/        # Profil, save, dan konten Disc 2 yang diimpor
├── cache/            # Cache game/shader
├── logs/             # Log setup, generate, build, dan startup
├── loaded-image.bin  # Image privat dari executable milik pengguna
├── local.json        # Path disc lokal
└── build-result.json # Hash artifact dan hasil build
```

Disc dibaca dari lokasi input; penulisan relatif ke direktori game dinonaktifkan. Save lama tidak disalin otomatis. Jangan commit atau unggah direktori `BuildRoot`.

## Yang sudah bekerja

Status pengujian: **13 Oktober 2026**. Hasil build portable terbaru dibedakan dari hasil gameplay historis karena binary, profil, dan pengaturannya berbeda.

### Terverifikasi pada build portable

- Setup SDK, generate dan build dari salinan bersih berkas Git, tanpa hasil recompilation lama; generate/build ulang juga berhasil.
- Executable utama, modul XMediaFacade dan SpeechFacade, runtime, serta plugin GPU berhasil dibangun.
- **271 tabel switch** dibentuk kembali dari input pengguna dan cocok dengan build sebelumnya.
- Set registrasi fungsi cocok dengan build sebelumnya: **50.233 utama, 3.212 XMedia, dan 6.715 Speech**. Ini tidak berarti setiap fungsi sudah diuji melalui gameplay.
- **5 tes skrip sintetis** dan **295.650 pemeriksaan instruksi** lulus tanpa kegagalan.
- Keempat paket Disc 2 berhasil diimpor ke profil baru.
- Logo dan video intro tampil; proses tetap berjalan selama 60 detik, lalu ditutup normal dengan exit code `0`.

Pengujian ini memakai source bersih pada komputer pengembangan yang sama, **belum komputer fisik kedua dan belum balapan penuh pada binary portable tersebut**. Lihat [hasil verifikasi](outputs/FM4-build/verification.json).

### Pernah berhasil pada build pengembangan sebelumnya

- Menu utama, World Tour, karier, Free Play/Quick Race, dan kontrol dasar kendaraan.
- Balapan karier Ford Ka di Road Atlanta sampai finish/Continue; progres dan saldo terbaca setelah restart.
- BMW M1 1981 dari Disc 2 sampai finish, termasuk uji otomatis tiga lap Sunset Peninsula Speedway.
- Autovista Ferrari 458 Italia: melihat mobil, membuka panel, masuk/keluar kokpit, mengganti cat, dan kembali ke menu.
- Simpan, overwrite, dan muat foto berwarna pada dua sesi baru setelah perbaikan filesystem dan penggunaan shader sinkron. Cache shader tidak dikosongkan pada uji tersebut.
- Rewind, Rewind Further, Resume dan Pause pada uji karier Tsukuba yang belum diselesaikan.
- Perbaikan texture/resolve dan layout MSAA memperbaiki pola piksel teracak serta menampilkan bayangan asli pada beberapa pengujian.

Hasil historis ini belum seluruhnya diuji ulang pada build portable terbaru. Detail dan cakupan bukti tersedia di [catatan status](outputs/FM4-build/STATUS.txt).

## Keterbatasan saat ini

- **Kompatibilitas terbatas:** Windows x64 dan revisi Japan/Asia yang diperiksa hash-nya. Belum mendukung setiap region atau title update.
- **Grafis belum sepenuhnya akurat:** cahaya matahari/bloom berlebihan masih pernah teramati; noise, refleksi, tepi objek, kombinasi MSAA dan skala resolusi perlu pengujian lebih luas. Workaround bayangan masih aktif di launcher.
- **Shader sinkron:** dipakai untuk menghindari draw terlewat yang pernah menyebabkan foto hitam. Dampak stutter/performa dan foto dengan cache shader kosong belum tuntas diverifikasi.
- **Input menu:** input diagnostik kadang terbaca tetapi belum membuat UI berpindah. Penyebab terkait fokus/waktu idle belum dipastikan. Pengemudi otomatis pengembangan bukan autopilot siap pakai.
- **Runtime belum lengkap:** masih ada layanan Xbox berupa stub. Tes instruksi tidak membuktikan seluruh kernel, audio, timing atau sinkronisasi benar.
- **Cakupan gameplay belum lengkap:** seluruh mobil/lintasan/mode, sesi panjang, audio, Kinect, multiplayer/online, dan DLC di luar Disc 2 belum tervalidasi menyeluruh. Belum diverifikasi tidak otomatis berarti rusak.
- **Save dan perangkat lain:** migrasi seluruh save antar-build/komputer, GPU lain, dan performa belum dijamin oleh pengujian saat ini.

Prioritas validasi berikutnya adalah komputer kedua, balapan karier sampai hadiah dan restart, balapan Disc 2 pada binary portable, serta pengujian foto dengan cache shader kosong. Daftar lengkap ada di [STATUS.txt](outputs/FM4-build/STATUS.txt).

## Jika setup atau build gagal

- Jalankan `doctor` terlebih dahulu. Pastikan `Python` menunjuk executable Python asli dan `LLVMRoot` menunjuk LLVM 20.x.
- Jika hash XEX ditolak, periksa versi game dan kelengkapan ekstraksi. Mengganti nama file tidak membuat revisi lain kompatibel.
- Jika aset tidak ditemukan, `Disc1` harus menunjuk root hasil ekstraksi lengkap, bukan folder berisi tiga XEX saja.
- Periksa log tahap yang gagal di `BuildRoot\logs`. Jangan menggabungkan runtime/plugin dari build berbeda; `run` memeriksa hash artifact.
- Jangan menerapkan patch historis di `outputs/FM4-analysis` di atas `outputs/FM4-build/sdk.patch`; workflow sudah memakai patch gabungan.
- Jika PowerShell memblokir skrip yang sudah kamu tinjau, contoh pemanggilan dengan execution policy khusus proses tersedia dalam [panduan build lengkap](outputs/FM4-build/README.txt).

## Isi repo dan atribusi

Repo menyimpan kode integrasi, tooling, tes sintetis, metadata lokasi/batas fungsi, resep switch, dan patch/source SDK. Instruksi asli game, isi tabel switch, aset, shader game, hasil recompilation, binary, save, serta capture tidak disertakan. Instruksi dan isi tabel dibaca dari data milik pengguna saat generate.

SDK dan shader resolve yang disertakan mempertahankan atribusi serta lisensi upstream:

- [Pemberitahuan ReXGlue/Xenia](outputs/FM4-source-control/third-party-notices/ReXGlue-LICENSE.txt)
- [Lisensi shader Xenia](outputs/FM4-build/sdk-extra/src/graphics/shaders/fm4-resolve/LICENSE.xenia)
- [Asal dan adaptasi source shader resolve](outputs/FM4-build/sdk-extra/src/graphics/shaders/fm4-resolve/PROVENANCE.txt)

Repo ini tidak menetapkan lisensi proyek menyeluruh yang baru dan tidak memberikan hak atas konten FM4. Daftar berkas yang ditinjau dicatat dalam [tracked-files.json](outputs/FM4-source-control/tracked-files.json). `.gitignore` mengabaikan berkas baru secara bawaan; penambahan source memerlukan pembaruan daftar tersebut dan peninjauan isi.

Sebelum commit perubahan berikutnya, periksa isi staging dengan Python:

```powershell
& 'C:\Python311\python.exe' .\outputs\FM4-source-control\check-staged-content.py
```

Pemeriksaan ini membantu mencegah penambahan tak sengaja; asal-usul perubahan tetap perlu ditinjau. Hook pre-commit lokal tidak otomatis ikut terpasang saat clone.
