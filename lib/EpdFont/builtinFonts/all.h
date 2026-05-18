#pragma once

// Reading fonts have generated variants with identical variable names:
//   default: emoji/symbol fallback + PHM CJK fallback
//   OMIT_PHM: emoji/symbol fallback, no PHM CJK
//   OMIT_EMOJI_FONTS: primary fonts only, no emoji and no PHM CJK
//
// Generate the variants with lib/EpdFont/scripts/convert-builtin-fonts.sh.
//
// Per-size guards:
//   OMIT_TEENSY_FONT - excludes 8px (Teensy) reading fonts; used by env:xlarge
//   OMIT_TINY_FONT   - excludes 10px (Tiny) reading fonts; used by env:xlarge
//   OMIT_SMALL_FONT  - excludes 12px (Small) reading fonts
#ifdef OMIT_EMOJI_FONTS
#define BUILTIN_READING_FONT_HEADER(name) <builtinFonts/noemoji/name.h>
#elif defined(OMIT_PHM)
#define BUILTIN_READING_FONT_HEADER(name) <builtinFonts/nophm/name.h>
#else
#define BUILTIN_READING_FONT_HEADER(name) <builtinFonts/name.h>
#endif

#ifndef OMIT_TEENSY_FONT
#include BUILTIN_READING_FONT_HEADER(bitter_8_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_8_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_8_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_8_regular)
#endif
#ifndef OMIT_TINY_FONT
#include BUILTIN_READING_FONT_HEADER(bitter_10_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_10_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_10_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_10_regular)
#endif
#ifndef OMIT_SMALL_FONT
#include BUILTIN_READING_FONT_HEADER(bitter_12_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_12_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_12_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_12_regular)
#endif
#include BUILTIN_READING_FONT_HEADER(bitter_14_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_14_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_14_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_14_regular)
#include BUILTIN_READING_FONT_HEADER(bitter_16_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_16_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_16_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_16_regular)

#ifndef OMIT_TEENSY_FONT
#include BUILTIN_READING_FONT_HEADER(lexenddeca_8_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_8_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_8_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_8_regular)
#endif
#ifndef OMIT_TINY_FONT
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_regular)
#endif
#ifndef OMIT_SMALL_FONT
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_regular)
#endif
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_regular)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_regular)

#undef BUILTIN_READING_FONT_HEADER

// Quicksand reading font - single variant (no emoji/PHM/symbol fallback faces),
// so direct includes work regardless of OMIT_EMOJI_FONTS / OMIT_PHM.
// Italic/BoldItalic slots are generated from Quicksand-Medium / Quicksand-Bold
// because Quicksand ships no italic master.
#ifndef OMIT_TEENSY_FONT
#include <builtinFonts/quicksand_8_bold.h>
#include <builtinFonts/quicksand_8_bolditalic.h>
#include <builtinFonts/quicksand_8_italic.h>
#include <builtinFonts/quicksand_8_regular.h>
#endif
#ifndef OMIT_TINY_FONT
#include <builtinFonts/quicksand_10_bold.h>
#include <builtinFonts/quicksand_10_bolditalic.h>
#include <builtinFonts/quicksand_10_italic.h>
#include <builtinFonts/quicksand_10_regular.h>
#endif
#ifndef OMIT_SMALL_FONT
#include <builtinFonts/quicksand_12_bold.h>
#include <builtinFonts/quicksand_12_bolditalic.h>
#include <builtinFonts/quicksand_12_italic.h>
#include <builtinFonts/quicksand_12_regular.h>
#endif
#include <builtinFonts/quicksand_14_bold.h>
#include <builtinFonts/quicksand_14_bolditalic.h>
#include <builtinFonts/quicksand_14_italic.h>
#include <builtinFonts/quicksand_14_regular.h>
#include <builtinFonts/quicksand_16_bold.h>
#include <builtinFonts/quicksand_16_bolditalic.h>
#include <builtinFonts/quicksand_16_italic.h>
#include <builtinFonts/quicksand_16_regular.h>

// UI fonts - Quicksand SemiBold (regular slot) + Quicksand Bold (bold slot).
// No emoji or PHM variants; UI text is short labels only.
#include <builtinFonts/quicksand_sb_10_bold.h>
#include <builtinFonts/quicksand_sb_10_regular.h>
#include <builtinFonts/quicksand_sb_12_bold.h>
#include <builtinFonts/quicksand_sb_12_regular.h>
#include <builtinFonts/quicksand_sb_8_regular.h>
