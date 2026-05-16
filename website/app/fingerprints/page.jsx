import { onMount } from "@opentf/web";

export default function FingerprintsRedirectPage() {
  onMount(() => {
    window.location.replace("/protection");
  });

  return (
    <main style="padding: 2rem; text-align: center;">
      <h1>Browser Protection Tests</h1>
      <p>Redirecting to the unified browser protection test center...</p>
      <a href="/protection">Open protection tests</a>
    </main>
  );
}
