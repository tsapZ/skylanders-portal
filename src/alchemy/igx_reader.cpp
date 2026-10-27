#include "alchemy/igx_reader.h"

namespace skylanders::alchemy {
namespace {

std::string Unescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out.push_back(s[i]);
      continue;
    }
    static constexpr std::pair<std::string_view, char> kEntities[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    bool replaced = false;
    for (const auto& [entity, c] : kEntities) {
      if (s.substr(i, entity.size()) == entity) {
        out.push_back(c);
        i += entity.size() - 1;
        replaced = true;
        break;
      }
    }
    if (!replaced) out.push_back('&');
  }
  return out;
}

// The value of attribute `name` inside one tag, or nullopt-like empty with found = false.
bool Attribute(std::string_view tag, std::string_view name, std::string* out) {
  size_t at = 0;
  while ((at = tag.find(name, at)) != std::string_view::npos) {
    const bool starts_word = at == 0 || tag[at - 1] == ' ' || tag[at - 1] == '\t';
    const size_t eq = at + name.size();
    if (starts_word && eq + 1 < tag.size() && tag[eq] == '=' && tag[eq + 1] == '"') {
      const size_t end = tag.find('"', eq + 2);
      if (end == std::string_view::npos) return false;
      *out = Unescape(tag.substr(eq + 2, end - eq - 2));
      return true;
    }
    at = eq;
  }
  return false;
}

std::string StripThis(std::string ref) {
  if (ref.rfind("this.", 0) == 0) ref.erase(0, 5);
  return ref;
}

}  // namespace

const std::string& IgxObject::Var(const std::string& name) const {
  static const std::string kEmpty;
  auto it = vars.find(name);
  return it == vars.end() ? kEmpty : it->second;
}

std::vector<IgxObject> ParseIgx(std::string_view text) {
  std::vector<IgxObject> objects;
  IgxObject* current = nullptr;
  size_t at = 0;
  while ((at = text.find('<', at)) != std::string_view::npos) {
    const size_t end = text.find('>', at);
    if (end == std::string_view::npos) break;
    const std::string_view tag = text.substr(at + 1, end - at - 1);
    at = end + 1;
    if (tag.rfind("object ", 0) == 0) {
      objects.emplace_back();
      current = &objects.back();
      Attribute(tag, "refname", &current->refname);
      Attribute(tag, "type", &current->type);
    } else if (tag == "/object") {
      current = nullptr;
    } else if (current && tag.rfind("var ", 0) == 0) {
      std::string name, value;
      if (!Attribute(tag, "name", &name)) continue;
      if (Attribute(tag, "value", &value) || Attribute(tag, "ref", &value)) {
        current->vars[name] = value;
      }
    } else if (current && tag.rfind("listitem ", 0) == 0) {
      std::string ref;
      if (Attribute(tag, "ref", &ref)) current->items.push_back(StripThis(ref));
    } else if (current && tag.rfind("dictitem ", 0) == 0) {
      std::string key, value;
      if (!Attribute(tag, "key", &key)) continue;
      if (Attribute(tag, "value", &value) || Attribute(tag, "ref", &value)) {
        current->dict.emplace_back(std::move(key), StripThis(std::move(value)));
      }
    }
  }
  return objects;
}

}  // namespace skylanders::alchemy
