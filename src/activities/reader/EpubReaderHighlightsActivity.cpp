#include "EpubReaderHighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "../ActivityResult.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;
constexpr int DELETE_MODE_OFF = 0;
constexpr int DELETE_MODE_DISPLAY = 1;
constexpr int DELETE_MODE_CONFIRM = 2;

constexpr int LINE_HEIGHT = 60;
constexpr size_t MAX_TITLE_CHARS = 120;
}  // namespace

void EpubReaderHighlightsActivity::rebuildSorted() {
  sorted = annotations.all();
  std::sort(sorted.begin(), sorted.end(),
            [](const AnnotationsManager::AnnotationRecord& a, const AnnotationsManager::AnnotationRecord& b) {
              if (a.sectionIdx != b.sectionIdx) return a.sectionIdx < b.sectionIdx;
              if (a.sectionPage != b.sectionPage) return a.sectionPage < b.sectionPage;
              return a.startText < b.startText;
            });
}

void EpubReaderHighlightsActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  rebuildSorted();
  requestUpdate();
}

void EpubReaderHighlightsActivity::onExit() { Activity::onExit(); }

int EpubReaderHighlightsActivity::getGutterBottom(const GfxRenderer& renderer) {
  const auto orientation = renderer.getOrientation();
  const bool isPortrait = orientation == GfxRenderer::Orientation::Portrait;
  return isPortrait ? 75 : 40;  // Reserve vertical space for button hints at the bottom
}

int EpubReaderHighlightsActivity::getListHeight(const GfxRenderer& renderer) {
  const auto pageHeight = renderer.getScreenHeight();
  return pageHeight - getGutterBottom(renderer) - LINE_HEIGHT;  // Reserve vertical space for title and hints
}

std::string EpubReaderHighlightsActivity::rowTitle(const AnnotationsManager::AnnotationRecord& rec) const {
  std::string text = rec.clipText;
  if (text.empty()) {
    // Legacy highlights (pre-v8) have no stored clip text; fall back to anchors.
    text = rec.startText;
    if (!rec.endText.empty() && rec.endText != rec.startText) text += " … " + rec.endText;
  }
  // Collapse newlines/tabs to spaces for a single-line row.
  std::replace_if(text.begin(), text.end(), [](char c) { return c == '\n' || c == '\r' || c == '\t'; }, ' ');
  if (text.size() > MAX_TITLE_CHARS) {
    text.resize(MAX_TITLE_CHARS);
    text += "…";
  }
  if (text.empty()) text = tr(STR_UNNAMED);
  return text;
}

std::string EpubReaderHighlightsActivity::rowSubtitle(const AnnotationsManager::AnnotationRecord& rec) const {
  const auto tocIndex = epub->getTocIndexForSpineIndex(rec.sectionIdx);
  const std::string tocTitle = (tocIndex >= 0) ? epub->getTocItem(tocIndex).title : tr(STR_UNNAMED);
  return std::string(tr(STR_PAGE)) + " " + std::to_string(rec.sectionPage + 1) + " - " + tocTitle;
}

void EpubReaderHighlightsActivity::loop() {
  const int numRecords = static_cast<int>(sorted.size());

  // Delete confirmation mode
  if (confirmingDelete >= DELETE_MODE_DISPLAY) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (confirmingDelete == DELETE_MODE_DISPLAY) {
        confirmingDelete = DELETE_MODE_CONFIRM;  // first confirmation, update text
        requestUpdate();
        return;
      }
      if (selectorIndex < numRecords) {
        annotations.removeById(sorted[selectorIndex].id);
        if (!annotations.save(bookCachePath.c_str())) {
          LOG_ERR("HLT", "Failed to save annotations after delete");
        }
        rebuildSorted();
      }
      // Move selector up if we deleted the last item
      if (selectorIndex >= static_cast<int>(sorted.size()) && selectorIndex > 0) {
        selectorIndex--;
      }
      requestUpdate();
      confirmingDelete = DELETE_MODE_OFF;
      return;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      requestUpdate();
      confirmingDelete = DELETE_MODE_OFF;
      return;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {  // Jump
    if (numRecords == 0) return;
    const auto& rec = sorted[selectorIndex];
    setResult(ProgressChangeResult{rec.sectionIdx, rec.sectionPage});
    finish();
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    if (numRecords == 0) return;
    confirmingDelete = DELETE_MODE_DISPLAY;
    requestUpdate();
  }

  buttonNavigator.onNextRelease([this, numRecords] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, numRecords);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, numRecords] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, numRecords);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, numRecords] {
    selectorIndex =
        ButtonNavigator::nextPageIndex(selectorIndex, numRecords, GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, numRecords] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, numRecords,
                                                       GUI.getListPageItems(getListHeight(renderer), true));
    requestUpdate();
  });
}

void EpubReaderHighlightsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& records = sorted;
  const int numRecords = static_cast<int>(records.size());

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int hintGutterBottom = getGutterBottom(renderer);
  const int contentY = hintGutterHeight;
  const int listY = contentY + LINE_HEIGHT;  // Reserve vertical space for title
  const int listHeight = getListHeight(renderer);

  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_HIGHLIGHTS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_HIGHLIGHTS), true, EpdFontFamily::BOLD);

  const auto getTitle = [this, &records](int index) {
    return rowTitle(records.at(confirmingDelete >= DELETE_MODE_DISPLAY ? selectorIndex : index));
  };
  const auto getSubtitle = [this, &records](int index) {
    return rowSubtitle(records.at(confirmingDelete >= DELETE_MODE_DISPLAY ? selectorIndex : index));
  };

  if (numRecords > 0) {
    if (confirmingDelete >= DELETE_MODE_DISPLAY) {
      GUI.drawHelpText(renderer, Rect{0, pageHeight / 2 - LINE_HEIGHT * 2, contentWidth, LINE_HEIGHT},
                       tr(STR_CONFIRM_DELETE_HIGHLIGHT));
      GUI.drawList(renderer, Rect{contentX, pageHeight / 2, contentWidth, LINE_HEIGHT}, 1, 0, getTitle, getSubtitle,
                   nullptr);
    } else {
      GUI.drawList(renderer, Rect{contentX, listY, contentWidth, listHeight}, numRecords, selectorIndex, getTitle,
                   getSubtitle, nullptr);
      GUI.drawHelpText(renderer, Rect{contentX, pageHeight - hintGutterBottom, contentWidth, LINE_HEIGHT},
                       tr(STR_HOLD_CONFIRM_TO_DELETE));
    }
  } else {
    GUI.drawHelpText(renderer, Rect{contentX, LINE_HEIGHT * 2, contentWidth, LINE_HEIGHT}, tr(STR_NO_HIGHLIGHTS));
  }

  const auto backLabel = confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_CANCEL) : tr(STR_BACK);
  const auto confirmLabel =
      numRecords > 0 ? (confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_DELETE) : tr(STR_OPEN)) : "";
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
