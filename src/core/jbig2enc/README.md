# jbig2enc (arithmetic coder only)

`jbig2arith.h` and `jbig2arith.cpp` are taken from
[jbig2enc](https://github.com/agl/jbig2enc) by Adam Langley / Google Inc.,
commit `d0dfca46216c98f11312a9c9f15615ed490cd7b3`, files `src/jbig2arith.h` and
`src/jbig2arith.cc`. They are licensed under the Apache License 2.0; see `LICENSE`
in this folder.

The PDF export uses them to compress black and white images losslessly as JBIG2
generic regions (`PdfImageEncoder::encodeJbig2`). The JBIG2 segments around the
coded data are written by ScanTailor's own code.

Changes made for ScanTailor OCR (2026):

* Removed everything that is not needed for lossless generic region coding:
  integer, IAID and out-of-bound coding (symbol and text regions), refinement
  coding, coding of byte-per-pixel images, `jbig2enc_reset` and `jbig2enc_flush`,
  and the corresponding context arrays in `jbig2enc_ctx`.
* The state table `ctbl` is `static const`.
* Renamed `jbig2arith.cc` to `jbig2arith.cpp`.
* Comments that described the removed parts or the data layout unclearly were
  corrected.
* The coded bytes go into one `std::vector<uint8_t>` (`jbig2enc_ctx::output`)
  instead of a list of 20 KiB chunks from `malloc`. This removes
  `JBIG2_OUTPUTBUFFER_SIZE` and `jbig2enc_dealloc()`: the context frees its
  memory when it is destroyed, and running out of memory throws
  `std::bad_alloc` instead of writing to a null pointer.
* `jbig2enc_bitimage()` encodes nothing for an image without pixels.
* Removed the debug branches `TRACE`, `CODER_DEBUGGING` and `SURPRISE_MAP`.
  `BRANCH_OPT` stays: with GCC and Clang it turns `likely`/`unlikely` into
  `__builtin_expect`.
* The header uses no reserved names: its include guard is
  `SCANTAILOR_CORE_JBIG2ENC_JBIG2ARITH_H_`, and the declarations no longer use
  `__restrict__` (which the header used to define for MSVC). It includes
  `<cstdint>` instead of choosing between `<sys/types.h>` (Solaris) and
  `<stdint.h>`.
* Changes for the static analysers (2026-10), none of them changes the coded
  output: the members of `jbig2enc_ctx` have `{}` initializers (cppcheck
  uninitMemberVarNoCtor); the static function `emit` is renamed to `emit_byte`,
  because with `--library=qt` cppcheck takes `emit` for Qt's macro
  (constStatement); the C-style cast of the image data in `jbig2enc_bitimage()`
  is a `reinterpret_cast`. The dead store `w3 = 0` there is kept and marked with
  a REVIEW-NOTE.
* Formatted with the repository's `.clang-format` like all other code. Only the
  state table `ctbl` is excluded (`// clang-format off`), so that it keeps one
  line per state as in Table E.1 of the standard.

To compare with upstream, format the upstream files with the same
`.clang-format` first, then compare, e.g.
`clang-format --style=file:.clang-format jbig2arith.cc > upstream.cpp`.

jbig2enc's `COPYING` notes that JBIG2 may be covered by patents (annex I of the
JBIG2 specification, ITU-T T.88 / ISO/IEC 14492 from 2000).
