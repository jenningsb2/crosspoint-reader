#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"
#include "annotations/AnnotationsManager.h"
#include "util/ButtonNavigator.h"

// Lists the current book's highlights: Confirm jumps to a highlight, hold-Confirm
// deletes it. Mirrors EpubReaderBookmarksActivity. Operates directly on the reader's
// AnnotationsManager (a reference that outlives this modal) so deletes persist via save.
class EpubReaderHighlightsActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  AnnotationsManager& annotations;
  std::string bookCachePath;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int confirmingDelete = 0;  // 0 = hide dialog, 1 = show dialog, 2 = allow confirmation to delete

  // Display copy of the highlights, sorted by reading position (section, then page).
  // Kept separate from the store so deletion still maps back by stable id.
  std::vector<AnnotationsManager::AnnotationRecord> sorted;

 public:
  EpubReaderHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::shared_ptr<Epub>& epub, AnnotationsManager& annotations,
                               const std::string& bookCachePath)
      : Activity("EpubReaderHighlights", renderer, mappedInput),
        epub(epub),
        annotations(annotations),
        bookCachePath(bookCachePath) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void rebuildSorted();
  int getGutterBottom(const GfxRenderer& renderer);
  int getListHeight(const GfxRenderer& renderer);
  std::string rowTitle(const AnnotationsManager::AnnotationRecord& rec) const;
  std::string rowSubtitle(const AnnotationsManager::AnnotationRecord& rec) const;
};
