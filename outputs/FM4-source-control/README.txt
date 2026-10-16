FM4 development: source-only repository
================================================

This repository records reviewed integration code, diagnostic tools, synthetic
tests and the project's runtime, a D3D12-only fork of the ReXGlue SDK. It does not contain the original game,
translated game function bodies, extracted content, game shaders or build output.

Included:
- work/fm4-rex/src: application integration, runtime adapters, diagnostics and
  test features (developer options fed to the title, rebuilt engageaionstart,
  guest vblank rate). Hook addresses identify externally supplied functions;
  their bodies are absent.
- work/fm4-rex/tests: synthetic arithmetic vectors and SDK texture-format tests.
  vector-vectors.h is generated from a mathematical oracle, not a game dump.
- CMake files and two local diagnostic helper scripts.
- work/runtime: ReXGlue SDK fork (BSD-3, Xenia-derived portions, notices kept).
  Third-party libraries are not stored; setup fetches them from the pinned
  upstream SDK revision. Inventory category runtime-fork; files may be up to
  512 KiB, and the recompiler emitter (function_graph.cpp) is marked so the
  translated-function pattern check skips it.
- outputs/FM4-build: the setup/generate/build/run pipeline (Build.ps1,
  pipeline.py, loader), reviewed integration metadata (function bounds, switch
  recipes), the e2e harness and gates (e2e/), README.txt, STATUS.txt and
  verification.json.
- outputs/FM4-analysis: seven selected SDK source patches, historical and
  already contained in work/runtime, and research notes (VISUAL-STYLE.txt,
  B4-SHADER-RESEARCH.txt).
- ReXGlue/Xenia copyright and BSD license notice in third-party-notices.

Excluded (kept on this computer, never staged for this checkpoint):
- Original discs, ISO/XEX files, extracted assets, loaded images and memory dumps.
- generated/, translated/disassembled game code, crash-function.cpp,
  original extracted tables and game shader analysis.
- SDK/third-party checkouts, executable/DLL/library files, build/cache directories.
- Captures, screenshots, logs, traces, saves, user data and existing archives.
- XenonRecomp-FM4-fixes.patch, pending a separate provenance/license review.

The portable setup/generate/build/run workflow is documented in
../FM4-build/README.txt. It builds work/runtime with third-party libraries from a
pinned open-source SDK revision and regenerates private game source using the
user's extracted discs.
The tracked compiler-template transform (FM4-build/guard-generated.py) has been
reviewed separately; it contains generic emitter transforms, not original game
function bodies. Reviewed numeric function locations/bounds and switch recipes
are included as integration metadata. Original switch contents are not included.
This repository still does not contain a runnable game or a complete backup of
the original development directory. Follow the portable workflow to rebuild.

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
by this workflow. Full gameplay compatibility remains incomplete.
