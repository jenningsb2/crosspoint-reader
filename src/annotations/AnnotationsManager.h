#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class AnnotationsManager {
 public:
  struct AnnotationRecord {
    uint32_t id = 0;
    uint16_t sectionIdx = 0;
    uint16_t sectionPage = 0;
    uint16_t endSectionPage = 0;
    uint16_t wordCount = 0;
    std::string startText;
    std::string endText;
    std::string beforeStartText;
    std::string afterEndText;
    std::string midText;
    std::string clipText;
  };

  bool load(const char* bookCachePath);
  bool save(const char* bookCachePath) const;

  // Stamps record.id from the per-book counter, then stores it.
  void add(AnnotationRecord record);
  size_t removeIf(const std::function<bool(const AnnotationRecord&)>& predicate);
  bool removeById(uint32_t id);

  const std::vector<AnnotationRecord>& all() const { return records; }
  std::vector<AnnotationRecord> forSection(uint16_t sectionIdx) const;
  bool hasAnnotationsForSection(uint16_t sectionIdx) const;
  bool empty() const { return records.empty(); }
  size_t size() const { return records.size(); }

 private:
  static constexpr uint8_t FILE_VERSION = 8;

  std::vector<AnnotationRecord> records;
  uint32_t nextId = 1;

  static std::string annotationsPath(const char* bookCachePath);
};
