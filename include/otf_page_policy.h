#ifndef OTF_BROWSER_PAGE_POLICY_H_
#define OTF_BROWSER_PAGE_POLICY_H_

#include <string>

namespace otf {

bool ShouldInjectPagePolicy(const std::string& url);

// Main-process-only: pick the stable screen profile (querying CefDisplay
// for the primary display, reading/writing the persisted choice under
// ~/.otf-browser/) and serialize it as JSON. The renderer must NOT call
// this — it touches the filesystem and querying CefDisplay there returns
// null. Call once in OnContextInitialized and hand the JSON to renderer
// processes via CefBrowserView extra_info; renderer process injects it
// into BuildPagePolicyScript().
std::string ResolveScreenProfileJson();

// Build the per-page policy script with a pre-resolved screen profile.
// Pure / side-effect-free — safe to call from the sandboxed renderer.
// If screen_profile_json is empty (extra_info missing or malformed), a
// hard-coded fallback profile is substituted so the script still runs.
std::string BuildPagePolicyScript(const std::string& screen_profile_json);

}  // namespace otf

#endif  // OTF_BROWSER_PAGE_POLICY_H_
