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

The code is kept in its original formatting so that it can be compared with
upstream; the Lint workflow skips this folder.

jbig2enc's `COPYING` notes that JBIG2 may be covered by patents (annex I of the
JBIG2 specification, ITU-T T.88 / ISO/IEC 14492 from 2000).
