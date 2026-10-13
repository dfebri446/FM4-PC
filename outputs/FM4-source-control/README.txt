FM4 development: selected source-only checkpoint
================================================

This repository records reviewed integration code, diagnostic tools, synthetic
tests and selected ReXGlue SDK patches. It does not contain the original game,
translated game function bodies, extracted content, game shaders or build output.

Included:
- work/fm4-rex/src: application integration, runtime adapters and diagnostics.
  Hook addresses identify externally supplied functions; their bodies are absent.
- work/fm4-rex/tests: synthetic arithmetic vectors and SDK texture-format tests.
  vector-vectors.h is generated from a mathematical oracle, not a game dump.
- CMake files and two local diagnostic helper scripts.
- outputs/FM4-analysis: seven selected SDK source patches. They are historical
  patches and are not guaranteed to cover every change in the local SDK checkout.
- ReXGlue/Xenia copyright and BSD license notice in third-party-notices.

Excluded (kept on this computer, never staged for this checkpoint):
- Original discs, ISO/XEX files, extracted assets, loaded images and memory dumps.
- generated/, translated/disassembled game code, crash-function.cpp,
  guard-generated.py, game-derived address/boundary tables and shader analysis.
- SDK checkout, executable/DLL/library files, build/cache directories.
- Captures, screenshots, logs, traces, saves, user data and existing archives.
- XenonRecomp-FM4-fixes.patch, pending a separate provenance/license review.

This is NOT a self-contained buildable game release or a complete source backup.
The application CMake file still refers to excluded local generated code and
build helpers. Those references are intentionally retained. A fresh checkout
needs separately obtained SDK dependencies and private game-derived inputs.
The standalone tests CMake file can be used with an external SDK header tree;
the texture-integer test additionally needs the relevant SDK implementation.

The root .gitignore ignores everything except individually reviewed paths.
tracked-files.json records the inventory and provenance categories. Before
adding a new path, review its origin, update both the inventory and .gitignore,
then run check-staged-content.py with Python against the staged Git index.
A local pre-commit hook runs that check in this working copy. Hooks are not
installed automatically by cloning. The check is an accident guard, not proof
of licensing provenance; even a listed source file needs review when edited.

No blanket license is assigned to the project by this checkpoint. Upstream
SDK-derived portions remain subject to the included upstream license notice.
No rights to FM4 game content are provided. No remote or upload is configured
by this initialization. Gameplay development remains paused.
