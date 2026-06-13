#pragma once

#include <annotations/AnnotationsManager.h>

#include <string>
#include <vector>

class ClippingsManager {
 public:
  // On-demand export of a book's highlights to the configured text clipping file
  // (Kindle "My Clippings.txt" format). Path resolves from SETTINGS: PER_BOOK overwrites
  // the per-book file; SINGLE_FILE appends to the shared log (preserving other books).
  // chapterTitles is parallel to records (one chapter title per record; may be empty).
  static bool exportText(const std::string& bookTitle, const std::string& author,
                         const std::vector<AnnotationsManager::AnnotationRecord>& records,
                         const std::vector<std::string>& chapterTitles);

  // On-demand export to a per-book JSON file (Readwise / Obsidian friendly). Always
  // overwrites /clippings/<title>.json. Streams one object per highlight.
  static bool exportJson(const std::string& bookTitle, const std::string& author,
                         const std::vector<AnnotationsManager::AnnotationRecord>& records,
                         const std::vector<std::string>& chapterTitles);

  // Returns the text clipping file path for the given book title based on current SETTINGS.
  static std::string resolveClippingPath(const std::string& bookTitle);

  // Returns the per-book JSON export path for the given book title.
  static std::string resolveJsonPath(const std::string& bookTitle);

  static constexpr const char* CLIPPINGS_PATH = "/My Clippings.txt";
  static constexpr const char* CLIPPINGS_DIR = "/clippings";
};
