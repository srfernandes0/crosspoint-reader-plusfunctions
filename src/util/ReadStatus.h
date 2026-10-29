#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// "Finished reading" marker for one book: a 6-byte read.bin inside the book's cache folder
// (/.crosspoint/epub_<hash>/). Renaming or moving the book on the device carries the folder
// along; clearing the book's cache removes the marker.
namespace ReadStatus {

struct Info {
  bool read = false;
  // False when the clock was not trustworthy at the time of marking.
  bool hasDate = false;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
};

// Only reflowable books (EPUB, TXT, MD) carry a marker.
bool supportsBook(const std::string& bookPath);

// Unmarked, unsupported or unreadable books return read = false.
Info load(const std::string& bookPath);

// Marks the book read with today's local date (no date when the clock is not set).
// On failure `info` is reset to unread and false is returned.
bool markRead(const std::string& bookPath, Info& info);

// Removes the marker. True when the book is unmarked afterwards.
bool clear(const std::string& bookPath);

// Writes a marker that is already known, for flows that wipe the cache folder.
bool restore(const std::string& bookPath, const Info& info);

// Translated "Read in DD/MM/YYYY", or "Read" without a date. Empty when the book is unread.
void formatLabel(const Info& info, char* buf, size_t bufSize);

}  // namespace ReadStatus
