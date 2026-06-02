#include "otf_utils.h"

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

namespace {

void TestIsSupportedDocumentUrl() {
  // PDFs
  assert(otf::IsSupportedDocumentUrl("report.pdf"));
  assert(otf::IsSupportedDocumentUrl("/path/to/file.PDF"));
  assert(otf::IsSupportedDocumentUrl("doc.pdf?v=1"));

  // Text
  assert(otf::IsSupportedDocumentUrl("notes.txt"));
  assert(otf::IsSupportedDocumentUrl("README.MD"));
  assert(otf::IsSupportedDocumentUrl("app.log"));

  // Data formats
  assert(otf::IsSupportedDocumentUrl("data.json"));
  assert(otf::IsSupportedDocumentUrl("config.json5"));
  assert(otf::IsSupportedDocumentUrl("config.jsonc"));
  assert(otf::IsSupportedDocumentUrl("schema.jsonld"));
  assert(otf::IsSupportedDocumentUrl("map.geojson"));
  assert(otf::IsSupportedDocumentUrl("stream.ndjson"));
  assert(otf::IsSupportedDocumentUrl("stream.ndjsonl"));
  assert(otf::IsSupportedDocumentUrl("config.hjson"));
  assert(otf::IsSupportedDocumentUrl("config.xml"));
  assert(otf::IsSupportedDocumentUrl("data.csv"));
  assert(otf::IsSupportedDocumentUrl("config.yaml"));
  assert(otf::IsSupportedDocumentUrl("config.yml"));
  assert(otf::IsSupportedDocumentUrl("config.toml"));

  // Code
  assert(otf::IsSupportedDocumentUrl("app.js"));
  assert(otf::IsSupportedDocumentUrl("index.html"));
  assert(otf::IsSupportedDocumentUrl("style.css"));
  assert(otf::IsSupportedDocumentUrl("main.py"));
  assert(otf::IsSupportedDocumentUrl("lib.rs"));
  assert(otf::IsSupportedDocumentUrl("main.go"));
  assert(otf::IsSupportedDocumentUrl("App.java"));
  assert(otf::IsSupportedDocumentUrl("app.rb"));
  assert(otf::IsSupportedDocumentUrl("script.sh"));
  assert(otf::IsSupportedDocumentUrl("query.sql"));
  assert(otf::IsSupportedDocumentUrl("main.c"));
  assert(otf::IsSupportedDocumentUrl("util.cpp"));
  assert(otf::IsSupportedDocumentUrl("header.h"));
  assert(otf::IsSupportedDocumentUrl("config.ini"));
  assert(otf::IsSupportedDocumentUrl("config.cfg"));
  assert(otf::IsSupportedDocumentUrl("config.conf"));
  assert(otf::IsSupportedDocumentUrl("Makefile"));
  assert(otf::IsSupportedDocumentUrl("style.tex"));

  // Makefile (no extension, lowercase 'makefile' check)
  assert(otf::IsSupportedDocumentUrl("makefile"));
  assert(otf::IsSupportedDocumentUrl("Makefile"));

  // Not supported
  assert(!otf::IsSupportedDocumentUrl("image.png"));
  assert(!otf::IsSupportedDocumentUrl("photo.jpg"));
  assert(!otf::IsSupportedDocumentUrl("video.mp4"));
  assert(!otf::IsSupportedDocumentUrl("audio.mp3"));
  assert(!otf::IsSupportedDocumentUrl("archive.zip"));
  assert(!otf::IsSupportedDocumentUrl("noext"));

  fprintf(stderr, "TestIsSupportedDocumentUrl PASSED\n");
}

void TestGuessDocumentMimeType() {
  assert(otf::GuessDocumentMimeType("report.pdf") == "application/pdf");
  assert(otf::GuessDocumentMimeType("data.json") == "application/json");
  assert(otf::GuessDocumentMimeType("data.json5") == "application/json5");
  assert(otf::GuessDocumentMimeType("data.jsonc") == "application/json");
  assert(otf::GuessDocumentMimeType("schema.jsonld") == "application/ld+json");
  assert(otf::GuessDocumentMimeType("map.geojson") == "application/geo+json");
  assert(otf::GuessDocumentMimeType("stream.ndjson") == "application/x-ndjson");
  assert(otf::GuessDocumentMimeType("stream.ndjsonl") == "application/x-ndjson");
  assert(otf::GuessDocumentMimeType("config.hjson") == "text/hjson");
  assert(otf::GuessDocumentMimeType("config.xml") == "application/xml");
  assert(otf::GuessDocumentMimeType("index.html") == "text/html");
  assert(otf::GuessDocumentMimeType("style.css") == "text/css");
  assert(otf::GuessDocumentMimeType("data.csv") == "text/csv");
  assert(otf::GuessDocumentMimeType("config.yaml") == "text/yaml");
  assert(otf::GuessDocumentMimeType("config.yml") == "text/yaml");
  assert(otf::GuessDocumentMimeType("notes.txt") == "text/plain");
  assert(otf::GuessDocumentMimeType("app.js") == "text/javascript");
  assert(otf::GuessDocumentMimeType("main.py") == "text/x-python");
  assert(otf::GuessDocumentMimeType("script.sh") == "text/x-shellscript");
  assert(otf::GuessDocumentMimeType("query.sql") == "text/x-sql");
  assert(otf::GuessDocumentMimeType("main.c") == "text/x-c");
  assert(otf::GuessDocumentMimeType("lib.rs") == "text/x-rust");
  assert(otf::GuessDocumentMimeType("main.go") == "text/x-go");
  assert(otf::GuessDocumentMimeType("App.java") == "text/x-java");
  assert(otf::GuessDocumentMimeType("app.rb") == "text/x-ruby");
  assert(otf::GuessDocumentMimeType("README.md") == "text/markdown");
  assert(otf::GuessDocumentMimeType("config.toml") == "text/plain");
  assert(otf::GuessDocumentMimeType("app.log") == "text/plain");

  fprintf(stderr, "TestGuessDocumentMimeType PASSED\n");
}

}  // namespace

int main() {
  TestIsSupportedDocumentUrl();
  TestGuessDocumentMimeType();
  fprintf(stderr, "All doc preview tests PASSED\n");
  return 0;
}
