#ifndef OTF_FIND_RUNTIME_H_
#define OTF_FIND_RUNTIME_H_

#include <string>

namespace otf {

std::string BuildFindResultEvent(int count,
                                 int active,
                                 int tab_id,
                                 const std::string& text,
                                 bool final_update,
                                 int seq = 0);
std::string BuildFindbarClosedEvent(int tab_id);

}  // namespace otf

#endif  // OTF_FIND_RUNTIME_H_
