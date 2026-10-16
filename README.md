# FM4-PC

Proyek eksperimental recompilation **Forza Motorsport 4 untuk Windows x64**, menggunakan ReXGlue SDK dan patch runtime/GPU berbasis ReXGlue/Xenia.

Proyek ini menyediakan kode integrasi serta alur setup, generate, build, dan menjalankan game dari **data game milik pengguna**. ReXGlue menerjemahkan kode executable Xbox 360 menjadi C++ secara lokal, lalu toolchain membangunnya menjadi executable dan DLL untuk PC. Layanan Xbox dan grafis tetap ditangani oleh runtime SDK.

**Status: dapat dibangun dari source bersih dan menjalankan Quick Race, mode foto, serta balapan karier sampai hadiah pada build terbaru (diuji otomatis lewat harness e2e), tetapi belum merupakan full port dengan seluruh fitur dan stabilitas terverifikasi.** Cakupan pengujiannya dijelaskan di bawah.

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

1. **Setup:** menyalin runtime proyek (`work/runtime`, fork SDK ReXGlue khusus D3D12) ke `BuildRoot`, mengambil library pihak ketiga dari SDK upstream pada commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` (13 submodule yang dibutuhkan D3D12), membangun shader resolve berlisensi, lalu membangun dan memasang runtime secara lokal.
2. **Generate:** memvalidasi executable milikmu, membaca image tanpa menjalankan kode guest, membentuk kembali tabel switch, dan menghasilkan C++ untuk executable utama serta kedua modul tambahan. Pemeriksaan hasil generate menolak diagnostik branch/label yang belum terselesaikan.
3. **Build:** menyinkronkan dan membangun ulang runtime secara inkremental, mengompilasi executable/DLL, menyalin runtime dan plugin GPU yang cocok, menjalankan pemeriksaan instruksi, dan menyimpan `build-result.json`.

`all` tidak langsung membuka game. Hasil generate dan seluruh binary berada di `BuildRoot`, bukan di source yang masuk Git.

Tahap-tahap tersebut juga dapat dijalankan terpisah:

```powershell
.\outputs\FM4-build\Build.ps1 -Action setup @common
.\outputs\FM4-build\Build.ps1 -Action generate @common @discs
.\outputs\FM4-build\Build.ps1 -Action build @common
```

Gunakan `BuildRoot` yang sama untuk seluruh tahap. Setelah mengubah source integrasi (`work/fm4-rex`), ulangi `generate` dan `build` agar salinan proyek lokal diperbarui; perubahan di `work/runtime` cukup dengan `build`.

## Menjalankan game

```powershell
.\outputs\FM4-build\Build.ps1 -Action run @common
```

Jika membuka PowerShell baru, definisikan kembali `$common` seperti contoh sebelumnya. Aksi `run` membaca lokasi disc yang disimpan saat `generate`.

Launcher memeriksa artifact build dan versi executable, menggunakan profil lokal baru, serta mengimpor empat paket Disc 2 jika path-nya diberikan. Impor pertama memerlukan waktu dan ruang tambahan. Shader sinkron aktif secara bawaan. Balapan memakai shadow map game di semua kamera (workaround bayangan Xenia lama hanya dengan `-ShadowWorkaround`), dan tekstur memakai anisotropic filtering 16x. Balapan memakai kualitas `high` (MSAA 4x dan setelan replay/photo mode, lihat `-Quality`). Tampilan memakai preset `pc`: mod buatan proyek pada shader game, yaitu post-processing (kurva tonemap ACES, saturasi 1,1, bloom 0,7, penajaman 0,4) dan clearcoat pada cat bodi mobil (kilau matahari GGX dan pantulan lingkungan di sudut miring); `-Look original` mengembalikan tampilan asli FM4. Game tampil layar penuh di monitor utama (`-Display native`) dan dirender pada kelipatan bulat 1280x720 terkecil yang menutup layar itu: 2x di 1080p, 1440p, dan 2560x1600, 3x di 4K. Rendering memakai jalur host render target (RTV): Quick Race stabil 60 fps dengan GPU sekitar 2,5 ms per frame balapan pada komputer pengembangan.

Opsi tambahan untuk `run` (rincian dan angka ukur ada di [panduan build lengkap](outputs/FM4-build/README.txt)):

| Opsi | Fungsi |
| --- | --- |
| `-FrameRate 120` (30-240) | Frame rate balapan di atas 60. Menu, replay, dan photo mode tetap memakai waktunya sendiri (vblank guest 60 Hz di luar balapan). Diukur: 118-120 fps pada 120, 220-235 fps pada 240. |
| `-Display native\|720p\|1080p\|1440p\|4k` | Layar dan resolusi render. `native` (bawaan): layar penuh di monitor utama. Ukuran lain: jendela sebesar itu (layar penuh dengan `-Fullscreen`, atau bila tidak muat di monitor). Skala render mengikuti layar: 720p 1x (1280x720), 1080p dan 1440p 2x (2560x1440), 4k 3x (3840x2160). Bila render lebih besar dari layar, presenter merata-ratakan semua piksel render per piksel layar (supersampling). |
| `-Supersample` | Render satu skala di atas layar (maksimal 4x, 5120x2880), lalu dirata-ratakan ke layar: tepi lebih halus dan benda tipis lebih stabil, dengan beban GPU skala itu. |
| `-ResolutionScale 1`-`4` | Skala render manual, menimpa pilihan `-Display`. |
| `-Info` | Menampilkan overlay setelan di pojok kiri atas sejak awal: layar dan resolusi render, kualitas, look dan kenopnya, bayangan, anisotropic filtering, frame rate, spion, jalur render, dan fps. Tombol F2 menampilkan atau menyembunyikannya di setiap run. Overlay digambar presenter, jadi tidak masuk capture e2e. |
| `-Quality xbox\|high\|ultra` | Kualitas render balapan. `high` (bawaan): MSAA 4x, soft particles, bayangan objek collidable, roda detail penuh, jarak LOD mobil 2x, tanpa pemangkasan balapan 16 mobil (seperti replay dan photo mode). `ultra`: semua mobil di LOD tertinggi dan spion setiap frame. `xbox`: setelan asli. Pada 240 Hz: xbox sekitar 220 fps, high 178-225 fps (rata-rata 201), ultra sekitar 145 fps; ketiganya stabil 60 fps. |
| `-Look <preset/kenop>` | Tampilan: preset `pc` (bawaan), `aces`, `gt`, `neutral` (kurva tonemap saja, dengan clearcoat cat mobil), `original` (tanpa mod), dapat digabung dengan kenop, misalnya `"pc,sharpen=0.6"`. Kenop: `tonemap` (filmic/gt/aces/neutral), `sharpen`, `bloom`, `exposure`, `contrast`, `saturation`, `vignette`, `motion_blur`; cat mobil: `clearcoat`, `clearcoat_roughness`, `clearcoat_glint`, `flakes`, `flake_scale`. |
| `-Rov` | Jalur render target lama (pixel shader interlock) untuk perbandingan. |
| `-SingleTile` | Balapan satu tile 720p tanpa MSAA, dengan FXAA. |
| `-FixedVblank` | Grid vblank 60 Hz tetap (tanpa vblank_vrr). |
| `-FullRateMirror` | Spion digambar setiap frame. |
| `-ShadowWorkaround` | Tanpa shadow map di luar kamera kokpit (workaround Xenia lama, seperti split screen). |

`-Action warmcache` mengisi cache shader dengan menjalankan timeline Quick Race sekali.

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
├── runtime-source/   # Salinan work/runtime + library pihak ketiga
├── runtime-upstream/ # Clone SDK upstream (hanya untuk library pihak ketiga)
├── runtime-build/    # Build runtime
├── runtime-install/  # Runtime yang dipasang lokal
├── project/          # Source integrasi dan generated/ privat
├── game-build/       # Executable, modul, runtime, plugin GPU
├── user-data/        # Profil, save, dan konten Disc 2 yang diimpor
├── cache/            # Cache game/shader
├── logs/             # Log setup, generate, build, dan startup
├── e2e/              # Hasil run e2e (capture, log, summary.json)
├── snapshots/        # Salinan game-build yang dipakai gate matrix.py
├── loaded-image.bin  # Image privat dari executable milik pengguna
├── local.json        # Path disc lokal
└── build-result.json # Hash artifact dan hasil build
```

Disc dibaca dari lokasi input; penulisan relatif ke direktori game dinonaktifkan. Save lama tidak disalin otomatis. Jangan commit atau unggah direktori `BuildRoot`.

## Yang sudah bekerja

Status pengujian: **16 Oktober 2026**. Hasil pada build terbaru dibedakan dari hasil gameplay historis karena binary, profil, dan pengaturannya berbeda.

### Terverifikasi otomatis pada build terbaru

Harness e2e menjalankan game dengan pengaturan launcher bawaan dan salinan profil, lalu memeriksa layar lewat OCR:

- Quick Race pada 60, 120, dan 240 Hz: 60,0 / 118-120 / 224-235 fps, tanpa crash dan tanpa frame jalan yang hilang.
- Simpan foto dari photo mode, lalu muat foto itu pada proses baru.
- Balapan karier World Tour, keluar lewat QUIT RACE, replay, lalu CONTINUE kembali ke event.
- Balapan karier Tsukuba (Ford Ka) sampai finis dengan AI FM4 sebagai pengemudi: hadiah kredit, level driver dan afinitas, EVENT COMPLETE, lalu progres dan saldo baru terbaca setelah restart (240 Hz).

### Terverifikasi pada build bersih (13-14 Oktober 2026)

- Setup, generate dan build dari salinan bersih berkas Git, tanpa hasil recompilation lama; generate/build ulang juga berhasil. Setelah runtime pindah ke `work/runtime`, setup/generate/build penuh di BuildRoot baru juga lolos.
- Executable utama, modul XMediaFacade dan SpeechFacade, runtime, serta plugin GPU berhasil dibangun.
- **271 tabel switch** dibentuk kembali dari input pengguna dan cocok dengan build sebelumnya.
- Set registrasi fungsi cocok dengan build sebelumnya: **50.233 utama, 3.212 XMedia, dan 6.715 Speech**. Ini tidak berarti setiap fungsi sudah diuji melalui gameplay.
- **5 tes skrip sintetis** dan **295.650 pemeriksaan instruksi** lulus tanpa kegagalan.
- Keempat paket Disc 2 berhasil diimpor ke profil baru.
- Logo dan video intro tampil; proses tetap berjalan selama 60 detik, lalu ditutup normal dengan exit code `0`.

Pengujian ini memakai source bersih pada komputer pengembangan yang sama, **belum komputer fisik kedua**. Lihat [hasil verifikasi](outputs/FM4-build/verification.json) (13 Oktober 2026).

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
- **Grafis belum sepenuhnya akurat:** koreksi empiris pada query occlusion matahari (faktor 2) hanya berlaku di jalur ROV (`-Rov`); di jalur bawaan RTV silau matahari belum dikalibrasi. Noise, refleksi, tepi objek, kombinasi MSAA dan skala resolusi perlu pengujian lebih luas.
- **Shader sinkron:** dipakai untuk menghindari draw terlewat yang pernah menyebabkan foto hitam. Dengan cache shader kosong hanya menambah hitch saat memuat, tanpa menurunkan fps balapan; foto dengan cache kosong belum diuji ulang.
- **Alat pengujian:** pengemudi otomatis di harness memakai AI FM4 sendiri (opsi developer `engageaionstart` yang dibangun ulang), bukan fitur untuk pemain. Menu FM4 mengabaikan tekanan pertama di beberapa layar dan menu karier bersembunyi setelah sekitar 9 detik tanpa input, jadi skenario e2e menekan sampai layar yang diharapkan terbaca.
- **Runtime belum lengkap:** masih ada layanan Xbox berupa stub. Tes instruksi tidak membuktikan seluruh kernel, audio, timing atau sinkronisasi benar.
- **Cakupan gameplay belum lengkap:** seluruh mobil/lintasan/mode, sesi panjang, audio, Kinect, multiplayer/online, dan DLC di luar Disc 2 belum tervalidasi menyeluruh. Belum diverifikasi tidak otomatis berarti rusak.
- **Save dan perangkat lain:** migrasi seluruh save antar-build/komputer, GPU lain, dan performa belum dijamin oleh pengujian saat ini.

Prioritas validasi berikutnya adalah komputer kedua, balapan mobil Disc 2 pada build terbaru, overwrite foto dan foto dengan cache shader kosong, serta Autovista. Daftar lengkap ada di [STATUS.txt](outputs/FM4-build/STATUS.txt).

## Pengujian otomatis

[e2e.py](outputs/FM4-build/e2e/e2e.py) menjalankan build dari `BuildRoot`, mengendalikan controller diagnostik, merekam capture serta trace performa per detik, dan menulis `summary.json`. Skenario (Quick Race, simpan dan muat foto, balapan karier, balapan karier sampai hadiah dan cek progres setelah restart) berjalan dengan timeline tetap dan cek layar OCR Windows, tanpa jaringan. [matrix.py](outputs/FM4-build/e2e/matrix.py) menjalankannya sebagai gate bertingkat (smoke sekitar 10 menit, perf, full sekitar 1 jam, career) pada salinan build, sehingga build berikutnya dapat dikompilasi selama gate berjalan, lalu membandingkannya dengan gate sebelumnya.

Opsi developer FM4 yang dikosongkan build retail dapat diisi lewat variabel `FM4_GAME_CMDLINE` (misalnya `engageaionstart` atau `nohud`). Hasil run berisi capture game, jadi simpan di luar repo (di bawah `BuildRoot`). Cara pakai lengkap ada di [e2e/README.txt](outputs/FM4-build/e2e/README.txt).

## Jika setup atau build gagal

- Jalankan `doctor` terlebih dahulu. Pastikan `Python` menunjuk executable Python asli dan `LLVMRoot` menunjuk LLVM 20.x.
- Jika hash XEX ditolak, periksa versi game dan kelengkapan ekstraksi. Mengganti nama file tidak membuat revisi lain kompatibel.
- Jika aset tidak ditemukan, `Disc1` harus menunjuk root hasil ekstraksi lengkap, bukan folder berisi tiga XEX saja.
- Periksa log tahap yang gagal di `BuildRoot\logs`. Jangan menggabungkan runtime/plugin dari build berbeda; `run` memeriksa hash artifact.
- Runtime ada di `work/runtime` (fork SDK ReXGlue, hanya D3D12). Patch historis di `outputs/FM4-analysis` tidak perlu diterapkan. BuildRoot lama dengan susunan `sdk-source` ditolak; buat BuildRoot baru.
- Jika PowerShell memblokir skrip yang sudah kamu tinjau, contoh pemanggilan dengan execution policy khusus proses tersedia dalam [panduan build lengkap](outputs/FM4-build/README.txt).

## Isi repo dan atribusi

Repo menyimpan kode integrasi, tooling, tes sintetis, metadata lokasi/batas fungsi, resep switch, dan runtime (fork SDK ReXGlue di `work/runtime`). Instruksi asli game, isi tabel switch, aset, shader game, hasil recompilation, binary, save, serta capture tidak disertakan. Instruksi dan isi tabel dibaca dari data milik pengguna saat generate.

Catatan riset tentang asal gaya visual FM4 (shader di kode game vs parameter di aset lintasan) ada di [VISUAL-STYLE.txt](outputs/FM4-analysis/VISUAL-STYLE.txt). Riset jalur shader yang bisa diedit (B4: HLSL lewat FXC, override per hash) ada di [B4-SHADER-RESEARCH.txt](outputs/FM4-analysis/B4-SHADER-RESEARCH.txt).

SDK dan shader resolve yang disertakan mempertahankan atribusi serta lisensi upstream:

- [Pemberitahuan ReXGlue/Xenia](outputs/FM4-source-control/third-party-notices/ReXGlue-LICENSE.txt)
- [Lisensi runtime (ReXGlue/Xenia)](work/runtime/LICENSE)
- [Lisensi shader Xenia](work/runtime/src/graphics/shaders/fm4-resolve/LICENSE.xenia)
- [Asal dan adaptasi source shader resolve](work/runtime/src/graphics/shaders/fm4-resolve/PROVENANCE.txt)

Repo ini tidak menetapkan lisensi proyek menyeluruh yang baru dan tidak memberikan hak atas konten FM4. Daftar berkas yang ditinjau dicatat dalam [tracked-files.json](outputs/FM4-source-control/tracked-files.json). `.gitignore` mengabaikan berkas baru secara bawaan; penambahan source memerlukan pembaruan daftar tersebut dan peninjauan isi.

Sebelum commit perubahan berikutnya, periksa isi staging dengan Python:

```powershell
& 'C:\Python311\python.exe' .\outputs\FM4-source-control\check-staged-content.py
```

Pemeriksaan ini membantu mencegah penambahan tak sengaja; asal-usul perubahan tetap perlu ditinjau. Hook pre-commit lokal tidak otomatis ikut terpasang saat clone.
