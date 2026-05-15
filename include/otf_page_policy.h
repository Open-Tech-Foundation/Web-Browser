#ifndef OTF_BROWSER_PAGE_POLICY_H_
#define OTF_BROWSER_PAGE_POLICY_H_

#include <string>

namespace otf {

bool ShouldInjectPagePolicy(const std::string& url);
std::string BuildPagePolicyScript();

}  // namespace otf

#endif  // OTF_BROWSER_PAGE_POLICY_H_
