#include "ClippingsManager.h"

#include <ArduinoJson.h>
#include <CrossPointSettings.h>
#include <HalStorage.h>
#include <Logging.h>
#include <common/FsApiConstants.h>

#include <algorithm>
#include <cctype>

static std::string sanitizeForFilename(const std::string& title) {
  std::string result;
  result.reserve(title.size());
  for (unsigned char c : title) {
    if (std::isalnum(c) || c == '-') {
      result += static_cast<char>(c);
    } else if (std::isspace(c)) {
      result += '_';
    }
  }
  // Limit filename length to avoid SD card path issues
  if (result.size() > 60) result.resize(60);
  if (result.empty()) result = "Unknown";
  return result;
}

std::string ClippingsManager::resolveClippingPath(const std::string& bookTitle) {
  if (SETTINGS.clippingStorage == CrossPointSettings::PER_BOOK) {
    return std::string(CLIPPINGS_DIR) + "/" + sanitizeForFilename(bookTitle) + ".txt";
  }
  return CLIPPINGS_PATH;
}

std::string ClippingsManager::resolveJsonPath(const std::string& bookTitle) {
  return std::string(CLIPPINGS_DIR) + "/" + sanitizeForFilename(bookTitle) + ".json";
}

// Builds one Kindle-style clipping block for a single highlight.
static std::string formatTextBlock(const std::string& bookTitle, const std::string& author,
                                   const std::string& chapterTitle, int pageNumber, const std::string& text) {
  static constexpr size_t MAX_TEXT = 2000;
  const size_t textLen = text.size() < MAX_TEXT ? text.size() : MAX_TEXT;

  std::string location = "- Your Highlight on Page " + std::to_string(pageNumber);
  if (!chapterTitle.empty()) {
    location += " | " + chapterTitle;
  }

  std::string block;
  block.reserve(bookTitle.size() + author.size() + location.size() + textLen + 40);
  block += bookTitle + " (" + author + ")\n";
  block += location + "\n\n";
  block.append(text, 0, textLen);
  block += "\n==========\n";
  return block;
}

bool ClippingsManager::exportText(const std::string& bookTitle, const std::string& author,
                                  const std::vector<AnnotationsManager::AnnotationRecord>& records,
                                  const std::vector<std::string>& chapterTitles) {
  const std::string path = resolveClippingPath(bookTitle);

  if (SETTINGS.clippingStorage == CrossPointSettings::PER_BOOK) {
    Storage.mkdir(CLIPPINGS_DIR);
  }

  // PER_BOOK: regenerate the book's file (idempotent). SINGLE_FILE: append so other
  // books' clippings in the shared log are preserved (Kindle semantics).
  const int flags = SETTINGS.clippingStorage == CrossPointSettings::PER_BOOK ? (O_RDWR | O_CREAT | O_TRUNC)
                                                                             : (O_RDWR | O_CREAT | O_AT_END);
  HalFile file = Storage.open(path.c_str(), flags);
  if (!file) {
    LOG_ERR("CLIP", "Failed to open %s for export", path.c_str());
    return false;
  }

  bool ok = true;
  for (size_t i = 0; i < records.size(); ++i) {
    const auto& rec = records[i];
    if (rec.clipText.empty()) continue;
    const std::string chapter = i < chapterTitles.size() ? chapterTitles[i] : std::string();
    const std::string block = formatTextBlock(bookTitle, author, chapter, rec.sectionPage + 1, rec.clipText);
    if (file.write(block.data(), block.size()) != block.size()) {
      ok = false;
      break;
    }
  }

  file.flush();
  file.close();
  if (!ok) {
    LOG_ERR("CLIP", "Failed to write text export to %s (SD full or removed?)", path.c_str());
    return false;
  }
  LOG_DBG("CLIP", "Exported %zu highlights (text) to %s", records.size(), path.c_str());
  return true;
}

bool ClippingsManager::exportJson(const std::string& bookTitle, const std::string& author,
                                  const std::vector<AnnotationsManager::AnnotationRecord>& records,
                                  const std::vector<std::string>& chapterTitles) {
  Storage.mkdir(CLIPPINGS_DIR);
  const std::string path = resolveJsonPath(bookTitle);

  HalFile file = Storage.open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("CLIP", "Failed to open %s for export", path.c_str());
    return false;
  }

  bool ok = true;
  auto writeStr = [&](const char* s, size_t n) {
    if (ok && file.write(s, n) != n) ok = false;
  };

  // Stream one JSON object per highlight to keep RAM flat (no whole-document buffer).
  const std::string head = "{\"book\":\"" + bookTitle + "\",\"author\":\"" + author + "\",\"highlights\":[";
  writeStr(head.data(), head.size());

  bool first = true;
  for (size_t i = 0; i < records.size() && ok; ++i) {
    const auto& rec = records[i];
    JsonDocument doc;
    doc["id"] = rec.id;
    doc["sectionIdx"] = rec.sectionIdx;
    doc["page"] = rec.sectionPage + 1;
    doc["chapter"] = i < chapterTitles.size() ? chapterTitles[i] : std::string();
    doc["text"] = rec.clipText;

    String out;
    serializeJson(doc, out);
    if (!first) writeStr(",", 1);
    first = false;
    writeStr(out.c_str(), out.length());
  }

  writeStr("]}", 2);

  file.flush();
  file.close();
  if (!ok) {
    LOG_ERR("CLIP", "Failed to write JSON export to %s", path.c_str());
    return false;
  }
  LOG_DBG("CLIP", "Exported %zu highlights (JSON) to %s", records.size(), path.c_str());
  return true;
}
