#pragma once

#include <annotations/AnnotationsManager.h>

#include <string>
#include <vector>

class ClippingsManager {
 public:
  // Kindle-style running log: append ONE highlight (Kindle block format) to the shared
  // /My Clippings.txt at creation time. Append-only — deletions are never reflected.
  // Gated by the SETTINGS.clippingLog toggle at the call site.
  static bool appendToLog(const std::string& bookTitle, const std::string& author, const std::string& chapterTitle,
                          int pageNumber, const std::string& selectedText);

  // On-demand clean export of a book's highlights to a per-book text file
  // (/clippings/<title>.txt, Kindle block format). Always overwrites — deletion-accurate,
  // never duplicates. chapterTitles is parallel to records (may be empty).
  static bool exportText(const std::string& bookTitle, const std::string& author,
                         const std::vector<AnnotationsManager::AnnotationRecord>& records,
                         const std::vector<std::string>& chapterTitles);

  // On-demand export to a per-book JSON file (Readwise / Obsidian friendly). Always
  // overwrites /clippings/<title>.json. Streams one object per highlight.
  static bool exportJson(const std::string& bookTitle, const std::string& author,
                         const std::vector<AnnotationsManager::AnnotationRecord>& records,
                         const std::vector<std::string>& chapterTitles);

  // Returns the per-book text export path (/clippings/<title>.txt) for the given title.
  static std::string resolveClippingPath(const std::string& bookTitle);

  // Returns the per-book JSON export path for the given book title.
  static std::string resolveJsonPath(const std::string& bookTitle);

  static constexpr const char* CLIPPINGS_PATH = "/My Clippings.txt";
  static constexpr const char* CLIPPINGS_DIR = "/clippings";
};
