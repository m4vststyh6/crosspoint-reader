#include "DictLayout.h"

#include <Utf8.h>

#include <numeric>

#include "IpaUtils.h"

namespace DictLayout {

void wrapSpans(const std::vector<StyledSpan>& spans, const WrapMetrics& metrics, const Measurer& measure,
               const LineSink& sink) {
  std::vector<IpaTextSpan> ipaRuns;
  const int maxWidth = metrics.maxWidth;
  const int indentStep = metrics.indentStep;
  const int bulletWidth = metrics.bulletWidth;

  LayoutLine currentLine;
  int currentX = 0;

  // Width of a string, accounting for mixed IPA/non-IPA runs (each run measured
  // with the appropriate font via the injected measurer).
  auto getMixedWidth = [&](const char* text, EpdFontFamily::Style style) {
    ipaRuns.clear();
    splitIpaRuns(text, ipaRuns);
    return std::accumulate(ipaRuns.begin(), ipaRuns.end(), 0, [&](int sum, const IpaTextSpan& run) {
      return sum + measure(run.text.c_str(), style, run.isIpa);
    });
  };

  auto flushLine = [&]() {
    if (!currentLine.segments.empty()) {
      sink(std::move(currentLine));
      currentLine = LayoutLine{};
    }
  };

  auto startLine = [&](uint8_t indent, bool listItem) {
    currentLine.indentLevel = indent;
    currentLine.isListItem = listItem;
    currentX = indent * indentStep + (listItem ? bulletWidth : 0);
  };

  auto appendToLine = [&](const std::string& text, EpdFontFamily::Style style, bool isIpa, int width) {
    if (!currentLine.segments.empty() && currentLine.segments.back().style == style &&
        currentLine.segments.back().isIpa == isIpa) {
      currentLine.segments.back().text += text;
    } else {
      currentLine.segments.push_back({text, style, isIpa});
    }
    currentX += width;
  };

  auto appendMixed = [&](const char* text, EpdFontFamily::Style style) {
    ipaRuns.clear();
    splitIpaRuns(text, ipaRuns);
    for (const auto& run : ipaRuns) {
      appendToLine(run.text, style, run.isIpa, measure(run.text.c_str(), style, run.isIpa));
    }
  };

  // Break a single token at codepoint boundaries when it is wider than the available line width.
  // IPA combining-mark handling (pendingIsIpa) mirrors splitIpaRuns and must be preserved.
  auto breakToken = [&](const std::string& tok, EpdFontFamily::Style style, uint8_t indentLevel) {
    const auto* bp = reinterpret_cast<const uint8_t*>(tok.c_str());
    std::string pending;
    int pendingWidth = 0;
    bool pendingIsIpa = false;
    uint32_t cp;
    while ((cp = utf8NextCodepoint(&bp))) {
      const bool combining = utf8IsCombiningMark(cp);
      const bool cpIsIpa = combining ? pendingIsIpa : isIpaCodepoint(cp);
      if (pending.empty()) pendingIsIpa = cpIsIpa;
      char buf[4];
      const int cpLen = utf8EncodeCodepoint(cp, buf);
      std::string cpStr(buf, cpLen);
      const int cpWidth = measure(cpStr.c_str(), style, cpIsIpa);
      if (!pending.empty() && currentX + pendingWidth + cpWidth > maxWidth) {
        appendMixed(pending.c_str(), style);
        flushLine();
        startLine(indentLevel, false);
        pending.clear();
        pendingWidth = 0;
        pendingIsIpa = cpIsIpa;
      }
      pending += cpStr;
      pendingWidth += cpWidth;
    }
    if (!pending.empty()) appendMixed(pending.c_str(), style);
  };

  startLine(0, false);

  for (const auto& span : spans) {
    if (!span.text || span.text[0] == '\0') continue;

    EpdFontFamily::Style style;
    if (span.bold && span.italic) {
      style = EpdFontFamily::BOLD_ITALIC;
    } else if (span.bold) {
      style = EpdFontFamily::BOLD;
    } else if (span.italic) {
      style = EpdFontFamily::ITALIC;
    } else {
      style = EpdFontFamily::REGULAR;
    }
    if (span.underline) style = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::UNDERLINE);

    if (span.newlineBefore) {
      flushLine();
      startLine(span.indentLevel, span.isListItem);
    }

    const int spanWidth = getMixedWidth(span.text, style);
    if (currentX + spanWidth <= maxWidth) {
      // Fast path: entire span fits on the current line.
      appendMixed(span.text, style);
    } else {
      // Word-wrap within the span.
      const char* p = span.text;
      while (*p) {
        bool hadSpace = false;
        while (*p == ' ') {
          hadSpace = true;
          ++p;
        }
        if (!*p) break;

        const char* tokStart = p;
        while (*p && *p != ' ') ++p;
        std::string tok(tokStart, p - tokStart);

        bool lineIsEmpty = currentLine.segments.empty();
        std::string candidate = (!lineIsEmpty && hadSpace) ? " " + tok : tok;
        int candidateWidth = getMixedWidth(candidate.c_str(), style);

        if (currentX + candidateWidth > maxWidth && !lineIsEmpty) {
          flushLine();
          startLine(span.indentLevel, false);
          candidate = tok;
          candidateWidth = getMixedWidth(tok.c_str(), style);
        }

        if (currentX + candidateWidth > maxWidth) {
          breakToken(candidate, style, span.indentLevel);
        } else {
          appendMixed(candidate.c_str(), style);
        }
      }
    }
  }

  flushLine();
}

void wrapSpans(const std::vector<StyledSpan>& spans, const WrapMetrics& metrics, const Measurer& measure,
               std::vector<LayoutLine>& out) {
  out.clear();
  out.reserve(32);
  const LineSink sink{&out, [](void* ctx, LayoutLine&& line) {
                        static_cast<std::vector<LayoutLine>*>(ctx)->push_back(std::move(line));
                      }};
  wrapSpans(spans, metrics, measure, sink);
}

}  // namespace DictLayout
