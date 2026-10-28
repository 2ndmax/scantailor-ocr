// Copyright 2006 Google Inc. All Rights Reserved.
// Author: agl@imperialviolet.org (Adam Langley)
//
// Copyright (C) 2006 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Taken from jbig2enc (https://github.com/agl/jbig2enc, src/jbig2arith.h,
// commit d0dfca46216c98f11312a9c9f15615ed490cd7b3).
// Modified for ScanTailor OCR (2026), see README.md in this folder:
// only the parts for lossless generic region coding are kept.

#ifndef SCANTAILOR_CORE_JBIG2ENC_JBIG2ARITH_H_
#define SCANTAILOR_CORE_JBIG2ENC_JBIG2ARITH_H_

#include <cstdint>
#include <vector>

#define JBIG2_MAX_CTX 65536

// -----------------------------------------------------------------------------
// This is the context for the arithmetic encoder used in JBIG2. The coder is a
// state machine; only the states for coding generic region images are kept
// here (see README.md).
//
// The coded bytes are appended to a vector.
// -----------------------------------------------------------------------------
struct jbig2enc_ctx {
  // these are the current state of the arithmetic coder
  uint32_t c{};
  uint16_t a{};
  uint8_t ct{}, b{};
  int bp{};

  std::vector<uint8_t> output;       // the coded bytes so far
  uint8_t context[JBIG2_MAX_CTX]{};  // state machine context for encoding images
};

// -----------------------------------------------------------------------------
// Returns the number of bytes of output in the given context
//
// Before doing this you should make sure that jbig2enc_final() has been called
// -----------------------------------------------------------------------------
unsigned jbig2enc_datasize(const struct jbig2enc_ctx* ctx);

// -----------------------------------------------------------------------------
// Writes the output of the given context to a buffer. The buffer must be at
// least long enough to contain all the data (see _datasize)
// -----------------------------------------------------------------------------
void jbig2enc_tobuffer(const struct jbig2enc_ctx* ctx, uint8_t* buffer);

// -----------------------------------------------------------------------------
// Encodes a 1 bit image of mx x my pixels as a generic region (template 0).
//
// This is designed for Leptonica's 1bpp packed format images. Each row is some
// number of 32-bit words, stored in native byte order; the leftmost pixel is
// the most significant bit of the word's value. Rows of QImage bytes can't be
// passed directly on little-endian systems.
//
// *The pad bits at the end of each line must be zero.*
//
// duplicate_line_removal turns on typical prediction (TPGDON): a row equal to
// the one above is coded as a single bit. An image without pixels (mx or my
// not positive) encodes nothing.
//
// Throws std::bad_alloc if memory runs out.
// -----------------------------------------------------------------------------
void jbig2enc_bitimage(struct jbig2enc_ctx* ctx, const uint8_t* data, int mx, int my, bool duplicate_line_removal);

// -----------------------------------------------------------------------------
// Init a new context or reset a used one. The context frees its memory
// itself when it is destroyed.
// -----------------------------------------------------------------------------
void jbig2enc_init(struct jbig2enc_ctx* ctx);

// -----------------------------------------------------------------------------
// Flush any remaining arithmetic encoder context to the output.
// -----------------------------------------------------------------------------
void jbig2enc_final(struct jbig2enc_ctx* ctx);

#endif  // SCANTAILOR_CORE_JBIG2ENC_JBIG2ARITH_H_
