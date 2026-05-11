#include "otf_utils.h"

#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <sys/types.h>
#include <filesystem>

namespace otf {

std::string GetExecutableDir() {
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count != -1) {
    return std::string(dirname(result));
  }
  return "";
}

std::string GetHomeDir() {
  const char* home_dir = getenv("HOME");
  if (home_dir) {
    return std::string(home_dir);
  }
  
  struct passwd* pw = getpwuid(getuid());
  if (pw && pw->pw_dir) {
    return std::string(pw->pw_dir);
  }
  
  return "";
}

std::string GetSettingsFilePath() {
  std::string home = GetHomeDir();
  if (home.empty()) return "";
  
  std::filesystem::path settings_dir = std::filesystem::path(home) / ".otf-browser";
  if (!std::filesystem::exists(settings_dir)) {
    std::filesystem::create_directories(settings_dir);
  }
  
  return (settings_dir / "settings.json").string();
}

} // namespace otf
