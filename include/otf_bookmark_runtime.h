#ifndef OTF_BOOKMARK_RUNTIME_H_
#define OTF_BOOKMARK_RUNTIME_H_

#include <string>

namespace otf {

std::string BuildBookmarkStateEvent(int tab_id,
                                    const std::string& url,
                                    bool bookmarked);
std::string BuildBookmarkSyncEvent(int tab_id,
                                   const std::string& url,
                                   bool bookmarked);

}  // namespace otf

#endif  // OTF_BOOKMARK_RUNTIME_H_
