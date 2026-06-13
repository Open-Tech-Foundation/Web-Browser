#ifndef OTF_IMAGE_PREVIEW_RUNTIME_H_
#define OTF_IMAGE_PREVIEW_RUNTIME_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace otf {

inline constexpr size_t kMaxTiffInputBytes = 64 * 1024 * 1024;

std::string GuessImageMimeType(const std::string& url);
std::string BuildImageContentToken(int tab_id,
                                   const std::string& kind,
                                   int page,
                                   const std::string& name_hint);
std::vector<uint8_t> DecodeDataUrlBytes(const std::string& data_url);
std::string MimeTypeToPreviewFormat(const std::string& mime_type);
bool DecodeLocalTiffPreview(const std::string& file_path,
                            int page_index,
                            std::string* png_base64,
                            int* page_count,
                            std::string* error_reason);
int64_t GetFileSizeBytes(const std::string& file_path,
                         std::string* error_reason);
std::string GuessPreviewFormat(const std::string& url);

}  // namespace otf

#endif  // OTF_IMAGE_PREVIEW_RUNTIME_H_
