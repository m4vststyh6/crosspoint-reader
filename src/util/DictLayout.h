#pragma once

#include <DictHtmlRenderer.h>
#include <EpdFontFamily.h>

#include <cstdint>
#include <string>
#include <vector>

// Pure, renderer-independent layout of dictionary HTML spans into wrapped
// display lines. Width measurement is injected via Measurer, so this module has
// no dependency on GfxRenderer / CrossPointSettings / font IDs. That decoupling
// is what lets the wrap/pagination logic be unit-tested host-side with a
// deterministic fake measurer (Tier-A litmus), while the device supplies a
// real font-metric-backed measurer.
namespace DictLayout {

// A single styled run within a display line.
struct LayoutSegment {
  std::string text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  bool isIpa = false;  // true → render with the IPA font
};

// One wrapped display line, containing one or more styled segments.
struct LayoutLine {
  std::vector<LayoutSegment> segments;
  uint8_t indentLevel = 0;
  bool isListItem = false;
};

// Width measurement injection. fn returns the pixel width of `text` rendered at
// `style`, using the IPA font when isIpa is true and the body font otherwise.
// Function-pointer + ctx (NOT std::function) per the project's binary-size rules.
struct Measurer {
  void* ctx = nullptr;
  int (*fn)(void* ctx, const char* text, EpdFontFamily::Style style, bool isIpa) = nullptr;
  int operator()(const char* text, EpdFontFamily::Style style, bool isIpa) const { return fn(ctx, text, style, isIpa); }
};

// Pixel metrics for one wrap pass. maxWidth is the usable line width; indentStep
// is one indent level in px; bulletWidth is the list-item bullet prefix width.
struct WrapMetrics {
  int maxWidth = 0;
  int indentStep = 0;
  int bulletWidth = 0;
};

// Reference full-layout path: word-wrap every span into `out` (cleared first).
// Produces the entire definition's lines in one pass (non-streaming) — this is
// the differential oracle the streaming path will be checked against.
void wrapSpans(const std::vector<StyledSpan>& spans, const WrapMetrics& metrics, const Measurer& measure,
               std::vector<LayoutLine>& out);

// Number of pages for a given line count and page size. Always >= 1.
inline int paginate(int lineCount, int linesPerPage) {
  if (linesPerPage < 1) linesPerPage = 1;
  const int pages = (lineCount + linesPerPage - 1) / linesPerPage;
  return pages < 1 ? 1 : pages;
}

}  // namespace DictLayout
