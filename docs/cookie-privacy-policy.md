# Cookie Privacy Policy

OTF Browser applies a strict cookie privacy policy at the CEF network layer.
The policy is intentionally simple and user-visible:

- Third-party cookies are blocked outright.
- First-party cookies are allowed, but their maximum lifetime is 7 days.
- Session cookies are also capped to 7 days because this browser persists
  session cookies across restarts for normal profile continuity.
- Users may need to sign in again after the 7-day cap expires.

## Third-Party Cookie Blocking

A request is treated as third-party when the top page origin and resource origin
do not share the same host family. For example, `tracker.example` embedded on
`site.example` is third-party. Subdomains under the same host family are treated
as same-site so normal site-owned subresource flows keep working.

Blocking is enforced in multiple layers:

- Chromium's `profile.block_third_party_cookies` preference is enabled for
  every request context.
- `CefCookieAccessFilter::CanSendCookie` returns `false` for third-party
  cookies when CEF exposes the cookie callback.
- `CefCookieAccessFilter::CanSaveCookie` returns `false` for third-party
  response cookies when CEF exposes the cookie callback.
- Resource-request fallback logic strips outgoing third-party `Cookie` headers
  and deletes third-party `Set-Cookie` results observed on response headers.

The older renderer-side `document.cookie` shadowing for cross-origin frames is
still useful defense in depth, but network-level request handling is
authoritative for HTTP and `HttpOnly` cookies.

## First-Party Lifetime Cap

First-party cookies are not classified by name or purpose. Instead, every
first-party cookie receives the same maximum lifetime: 7 days from the time it
is observed.

If a first-party cookie has no expiry, or has an expiry beyond 7 days, the
browser allows the write and immediately rewrites the same cookie with a capped
expiry. If the cookie already expires within 7 days, the browser leaves it
unchanged.

This strict cap avoids fragile allow/deny guessing and gives users predictable
privacy behavior. The tradeoff is intentional: long-lived login sessions,
remember-me cookies, and durable preferences can expire after one week.

## Site Data Visibility

`browser://sitedata` shows cookie policy actions for the selected origin:

- third-party cookies blocked before send,
- third-party cookies blocked before save,
- first-party cookies whose expiry was capped,
- original expiry, imposed expiry, and event counts.

Clearing cookies for an origin also clears its recorded cookie-policy actions.

## Storage

Cookie policy actions are recorded in the existing `browser.db` SQLite database
in `cookie_policy_events`. The records are audit metadata only; cookie
enforcement is performed by CEF request/cookie callbacks.
