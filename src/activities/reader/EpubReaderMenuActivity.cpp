#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {

struct ReaderLayoutSettingsSnapshot {
  uint8_t fontFamily;
  uint8_t fontSize;
  uint8_t lineSpacing;
  uint8_t orientation;
  uint8_t screenMargin;
  uint8_t paragraphAlignment;
  uint8_t embeddedStyle;
  uint8_t hyphenationEnabled;
  uint8_t imageRendering;
  uint8_t extraParagraphSpacing;
  uint8_t forceParagraphIndents;
  bool operator==(const ReaderLayoutSettingsSnapshot& other) const {
    return fontFamily == other.fontFamily && fontSize == other.fontSize && lineSpacing == other.lineSpacing &&
           orientation == other.orientation && screenMargin == other.screenMargin &&
           paragraphAlignment == other.paragraphAlignment && embeddedStyle == other.embeddedStyle &&
           hyphenationEnabled == other.hyphenationEnabled && imageRendering == other.imageRendering &&
           extraParagraphSpacing == other.extraParagraphSpacing &&
           forceParagraphIndents == other.forceParagraphIndents;
  }
  bool operator!=(const ReaderLayoutSettingsSnapshot& other) const { return !(*this == other); }
};

ReaderLayoutSettingsSnapshot captureReaderLayoutSettings() {
  return {
      SETTINGS.fontFamily,
      SETTINGS.fontSize,
      SETTINGS.lineSpacing,
      SETTINGS.orientation,
      SETTINGS.screenMargin,
      SETTINGS.paragraphAlignment,
      SETTINGS.embeddedStyle,
      SETTINGS.hyphenationEnabled,
      SETTINGS.imageRendering,
      SETTINGS.extraParagraphSpacing,
      SETTINGS.forceParagraphIndents,
  };
}

bool haveReaderLayoutSettingsChanged(const ReaderLayoutSettingsSnapshot& before) {
  return before != captureReaderLayoutSettings();
}

}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks,
                                               const bool isCurrentPageBookmarked, const bool isBookCompleted)
    : Activity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes, hasBookmarks, isCurrentPageBookmarked, isBookCompleted)),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {}

std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::buildMenuItems(bool hasFootnotes,
                                                                                     bool hasBookmarks,
                                                                                     bool isCurrentPageBookmarked,
                                                                                     bool isBookCompleted) {
  std::vector<MenuItem> items;
  // 5 section headers + 13 base actions
  constexpr size_t baseItemCount = 18;
  const size_t totalItemCount = baseItemCount + (hasFootnotes ? 1u : 0u) + (hasBookmarks ? 2u : 0u);
  items.reserve(totalItemCount);

  const auto addHeader = [&items](StrId id) { items.push_back({MenuAction::SECTION_HEADER, id, true}); };

  addHeader(StrId::STR_READER_SECT_NAVIGATION);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});

  addHeader(StrId::STR_CAT_DISPLAY);
  items.push_back({MenuAction::READER_OPTIONS, StrId::STR_READER_OPTIONS});
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});

  addHeader(StrId::STR_BOOKMARKS);
  items.push_back(
      {MenuAction::BOOKMARK_TOGGLE, isCurrentPageBookmarked ? StrId::STR_REMOVE_BOOKMARK : StrId::STR_ADD_BOOKMARK});
  if (hasBookmarks) {
    items.push_back({MenuAction::VIEW_BOOKMARKS, StrId::STR_VIEW_BOOKMARKS});
    items.push_back({MenuAction::DELETE_BOOKMARKS, StrId::STR_DELETE_BOOKMARKS});
  }

  addHeader(StrId::STR_READER_SECT_READING);
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_INTERVAL_SECONDS});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  items.push_back({MenuAction::READING_STATS, StrId::STR_READING_STATS});
  items.push_back(
      {MenuAction::TOGGLE_COMPLETED, isBookCompleted ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});

  addHeader(StrId::STR_READER_SECT_TOOLS);
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});

  return items;
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { Activity::onExit(); }

void EpubReaderMenuActivity::loop() {
  // Handle navigation — skip section header rows
  const int itemCount = static_cast<int>(menuItems.size());
  buttonNavigator.onNext([this, itemCount] {
    int next = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    while (menuItems[next].isHeader) next = ButtonNavigator::nextIndex(next, itemCount);
    selectedIndex = next;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, itemCount] {
    int prev = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    while (menuItems[prev].isHeader) prev = ButtonNavigator::previousIndex(prev, itemCount);
    selectedIndex = prev;
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto selectedAction = menuItems[selectedIndex].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      // Cycle orientation preview locally; actual rotation happens on menu exit.
      pendingOrientation = (pendingOrientation + 1) % orientationLabels.size();
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::READER_OPTIONS) {
      const auto before = captureReaderLayoutSettings();
      startActivityForResult(std::make_unique<ReaderOptionsActivity>(renderer, mappedInput),
                             [this, before](const ActivityResult&) {
                               settingsChanged = settingsChanged || haveReaderLayoutSettingsChanged(before);
                               pendingOrientation = SETTINGS.orientation;  // sync in case orientation changed
                               requestUpdate();
                             });
      return;
    }

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, settingsChanged});
    finish();
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    result.data = MenuResult{-1, pendingOrientation, settingsChanged};
    setResult(std::move(result));
    finish();
    return;
  }
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;

  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";

  GUI.drawHeader(renderer,
                 Rect{contentX, hintGutterHeight + metrics.topPadding, contentWidth, metrics.headerHeight},
                 title.c_str(), progressLine.c_str());

  const int listTop = hintGutterHeight + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{contentX, listTop, contentWidth, listHeight},
      static_cast<int>(menuItems.size()), selectedIndex,
      [this](int i) { return std::string(I18N.get(menuItems[i].labelId)); },
      nullptr, nullptr,
      [this](int i) -> std::string {
        if (menuItems[i].action == MenuAction::ROTATE_SCREEN) {
          return std::string(I18N.get(orientationLabels[pendingOrientation]));
        }
        return "";
      },
      true, nullptr,
      [this](int i) -> bool { return menuItems[i].isHeader; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}
