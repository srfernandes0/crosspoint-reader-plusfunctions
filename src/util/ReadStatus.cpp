#include "ReadStatus.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <TrustedTime.h>
#include <time.h>

#include <cstdio>
#include <functional>
#include <string_view>

namespace ReadStatus {
namespace {

constexpr uint8_t FORMAT_VERSION = 1;
constexpr uint8_t FLAG_HAS_DATE = 0x01;
// version, flags, year (u16, little endian), month, day
constexpr size_t FILE_BYTES = 6;
constexpr size_t PATH_BYTES = 64;
// Same lower bound TrustedTime uses for "the clock was set"; 2100 is its upper bound.
constexpr uint16_t MIN_YEAR = 2025;
constexpr uint16_t MAX_YEAR = 2100;

// Mirrors the folder name Epub::Epub() builds: "/.crosspoint/epub_<hash of the book path>".
// hash<string_view> equals hash<string> for the same characters, and a fixed buffer keeps list
// repaints free of heap use.
void buildPath(const std::string& bookPath, const char* suffix, char* out, const size_t outSize) {
  const unsigned long key = static_cast<unsigned long>(std::hash<std::string_view>{}(bookPath));
  snprintf(out, outSize, "/.crosspoint/epub_%lu%s", key, suffix);
}

Info parse(const uint8_t* data) {
  Info info;
  info.read = true;
  const uint16_t year = static_cast<uint16_t>(data[2] | (data[3] << 8));
  const bool validDate = (data[1] & FLAG_HAS_DATE) != 0 && year >= MIN_YEAR && year <= MAX_YEAR && data[4] >= 1 &&
                         data[4] <= 12 && data[5] >= 1 && data[5] <= 31;
  if (validDate) {
    info.hasDate = true;
    info.year = year;
    info.month = data[4];
    info.day = data[5];
  }
  return info;
}

bool writeMarker(const std::string& bookPath, const Info& info) {
  char dir[PATH_BYTES];
  char tmpPath[PATH_BYTES];
  char finalPath[PATH_BYTES];
  buildPath(bookPath, "", dir, sizeof(dir));
  buildPath(bookPath, "/read.bin.tmp", tmpPath, sizeof(tmpPath));
  buildPath(bookPath, "/read.bin", finalPath, sizeof(finalPath));

  // The folder is missing for a book that was never opened, or after a cache wipe.
  if (!Storage.exists(dir)) Storage.mkdir(dir);

  const uint16_t year = info.hasDate ? info.year : 0;
  const uint8_t data[FILE_BYTES] = {FORMAT_VERSION,
                                    info.hasDate ? FLAG_HAS_DATE : static_cast<uint8_t>(0),
                                    static_cast<uint8_t>(year & 0xFF),
                                    static_cast<uint8_t>(year >> 8),
                                    info.hasDate ? info.month : static_cast<uint8_t>(0),
                                    info.hasDate ? info.day : static_cast<uint8_t>(0)};

  bool written = false;
  {
    HalFile file;
    if (Storage.openFileForWrite("RDS", tmpPath, file)) {
      written = file.write(data, FILE_BYTES) == FILE_BYTES;
      file.flush();
    }
    // The temp file closes at scope exit, before it is renamed over read.bin.
  }
  if (!written || !Storage.replaceFile(tmpPath, finalPath)) {
    LOG_ERR("RDS", "Could not save read status: %s", finalPath);
    Storage.remove(tmpPath);
    return false;
  }
  LOG_DBG("RDS", "Read status saved: %s (date %s)", finalPath, info.hasDate ? "yes" : "no");
  return true;
}

}  // namespace

bool supportsBook(const std::string& bookPath) { return FsHelpers::hasReflowableBookExtension(bookPath); }

Info load(const std::string& bookPath) {
  if (!supportsBook(bookPath)) return {};
  char path[PATH_BYTES];
  buildPath(bookPath, "/read.bin", path, sizeof(path));
  HalFile file = Storage.open(path);
  if (!file.isOpen()) return {};
  // One spare byte: a longer file is not a version 1 marker.
  uint8_t data[FILE_BYTES + 1];
  const int size = file.read(data, sizeof(data));
  if (size != static_cast<int>(FILE_BYTES) || data[0] != FORMAT_VERSION) return {};
  return parse(data);
}

bool markRead(const std::string& bookPath, Info& info) {
  info = Info{};
  info.read = true;
  const int64_t now = trustedtime::trustedNow();
  if (now > 0) {
    // The saved timezone is applied process-wide at boot (timezones::applyToClock).
    const time_t seconds = static_cast<time_t>(now);
    struct tm local;
    if (localtime_r(&seconds, &local) != nullptr) {
      info.year = static_cast<uint16_t>(local.tm_year + 1900);
      info.month = static_cast<uint8_t>(local.tm_mon + 1);
      info.day = static_cast<uint8_t>(local.tm_mday);
      info.hasDate = info.year >= MIN_YEAR && info.year <= MAX_YEAR;
    }
  }
  if (!supportsBook(bookPath) || !writeMarker(bookPath, info)) {
    info = Info{};
    return false;
  }
  return true;
}

bool clear(const std::string& bookPath) {
  if (!supportsBook(bookPath)) return false;
  char path[PATH_BYTES];
  buildPath(bookPath, "/read.bin", path, sizeof(path));
  if (!Storage.exists(path)) return true;
  const bool removed = Storage.remove(path);
  if (removed) LOG_DBG("RDS", "Read status cleared: %s", path);
  return removed;
}

bool restore(const std::string& bookPath, const Info& info) {
  return info.read && supportsBook(bookPath) && writeMarker(bookPath, info);
}

void formatLabel(const Info& info, char* buf, const size_t bufSize) {
  if (bufSize == 0) return;
  buf[0] = '\0';
  if (!info.read) return;
  if (!info.hasDate) {
    snprintf(buf, bufSize, "%s", tr(STR_READ_STATUS));
    return;
  }
  char date[16];
  snprintf(date, sizeof(date), "%02u/%02u/%04u", static_cast<unsigned>(info.day), static_cast<unsigned>(info.month),
           static_cast<unsigned>(info.year));
  snprintf(buf, bufSize, tr(STR_READ_IN_DATE), date);
}

}  // namespace ReadStatus
