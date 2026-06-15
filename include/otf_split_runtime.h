#ifndef OTF_SPLIT_RUNTIME_H_
#define OTF_SPLIT_RUNTIME_H_

namespace otf {

class TabManager;

bool IsSplitPlaceholderTab(TabManager* tab_manager, int tab_id);

}  // namespace otf

#endif  // OTF_SPLIT_RUNTIME_H_
