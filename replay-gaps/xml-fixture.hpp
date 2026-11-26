#pragma once
#include "common.hpp"
#include <libxml/parser.h>
#include <memory>

namespace row_replay {
struct FeedArticle {
  std::string editor, section, state, title;
};

inline std::vector<FeedArticle> read_feed(const std::string &data) {
  using miner::check;
  check(data.size() <= 1024 * 1024, "XML oracle fixture too large");
  const std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc(
      xmlReadMemory(data.data(), static_cast<int>(data.size()), "feed.xml",
                    nullptr, XML_PARSE_NONET),
      xmlFreeDoc);
  check(doc != nullptr, "could not parse XML feed");
  auto *feed = xmlDocGetRootElement(doc.get());
  check(feed && xmlStrEqual(feed->name, BAD_CAST "feed"), "expected feed root");
  auto text = [](xmlChar *raw) {
    const std::unique_ptr<xmlChar, decltype(xmlFree)> value(raw, xmlFree);
    check(value != nullptr, "missing XML field");
    return std::string(reinterpret_cast<const char *>(value.get()));
  };
  std::vector<FeedArticle> result;
  for (auto *article = xmlFirstElementChild(feed); article;
       article = xmlNextElementSibling(article)) {
    check(xmlStrEqual(article->name, BAD_CAST "article"), "expected article");
    const auto *title = xmlFirstElementChild(article);
    check(title && xmlStrEqual(title->name, BAD_CAST "title"),
          "expected title");
    result.push_back({text(xmlGetProp(article, BAD_CAST "editor")),
                      text(xmlGetProp(article, BAD_CAST "section")),
                      text(xmlGetProp(article, BAD_CAST "state")),
                      text(xmlNodeGetContent(title))});
  }
  return result;
}
} // namespace row_replay
