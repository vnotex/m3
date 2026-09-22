#include "test_support.h"
#include <future>
#include <exception>
#include <utility>
static void roundtrip() {
    M3Mindmap *raw = nullptr;
    ok(m3_mindmap_create("r", "Root", &raw));
    Map map(raw, m3_mindmap_destroy);
    auto j = document(map);
    CHECK(j["rootId"] == "r");
    CHECK(j["nodes"][0]["topic"] == "Root");
    auto copy = load(j);
    CHECK(document(copy) == j);
    auto input = fixture();
    auto rich = load(input);
    auto exported = document(rich);
    CHECK(exported["nodes"][0]["id"] == "r");
    CHECK(exported["nodes"][1]["id"] == "a");
    CHECK(exported["nodes"][2]["id"] == "d");
    for (const auto &n : input["nodes"]) {
        auto actual = node(rich, n["id"].get<std::string>().c_str());
        for (auto it = n.begin(); it != n.end(); ++it) CHECK(actual[it.key()] == it.value());
    }
    for (const auto &l : input["crossLinks"]) {
        auto actual = link(rich, l["id"].get<std::string>().c_str());
        for (auto it = l.begin(); it != l.end(); ++it) CHECK(actual[it.key()] == it.value());
    }
    CHECK(node(rich, "c") == Json::parse(R"({"id":"c","topic":"","note":"","hyperLink":"","style":{},"expanded":true,"tags":[],"icons":[],"image":null,"children":[]})"));
    CHECK(exported["crossLinks"][0]["id"] == "l1");
    auto rich_copy = load(exported);
    CHECK(document(rich_copy) == exported);
    auto unicode = load(Json::parse(R"({"schemaVersion":1,"rootId":"根🌍","nodes":[{"id":"根🌍","image":{"url":"","width":0,"height":0}}]})"));
    CHECK(document(unicode)["rootId"] == "根🌍");
    char *snapshot = nullptr;
    ok(m3_mindmap_to_json(rich.get(), &snapshot));
    Text owned(snapshot, m3_string_free);
    char *record = nullptr;
    ok(m3_mindmap_get_node_json(rich.get(), "r", &record));
    Text owned_node(record, m3_string_free);
    rich.reset();
    CHECK(Json::parse(owned.get()) == exported);
    CHECK(Json::parse(owned_node.get()) == exported["nodes"][0]);
}

static void outline() {
    auto input = fixture();
    input["nodes"][0]["topic"] = "Root \"世界\"\n\\ <b>*literal*</b>";
    input["nodes"][0]["children"] = {"c", "a", "b"};
    input["nodes"][0]["expanded"] = false;
    for (auto &n : input["nodes"]) if (n["id"] == "d") n["children"] = {"n0"};
    constexpr size_t count = 4096;
    for (size_t i = 0; i < count; ++i) {
        auto children = Json::array();
        if (i + 1 < count) children.push_back("n" + std::to_string(i + 1));
        if (i == 1) children.push_back("末");
        input["nodes"].push_back({{"id", "n" + std::to_string(i)},
            {"topic", "Chain " + std::to_string(i)}, {"children", std::move(children)}});
    }
    input["nodes"].push_back({{"id", "末"}});
    auto expected = Json::parse(R"({"id":"r","topic":"","children":[
      {"id":"c","topic":"","children":[]},
      {"id":"a","topic":"","children":[
        {"id":"d","topic":"","children":[
          {"id":"n0","topic":"Chain 0","children":[
            {"id":"n1","topic":"Chain 1","children":[
              {"id":"n2","topic":"Chain 2","children":[]},
              {"id":"末","topic":"","children":[]}]}]}]}]},
      {"id":"b","topic":"","children":[]}]})");
    expected["topic"] = input["nodes"][0]["topic"];
    auto map = load(input);
    const auto before = document(map);
    char *raw = reinterpret_cast<char *>(1);
    CHECK(m3_mindmap_get_outline_json(nullptr, &raw) == M3_ERR_INVALID_ARGUMENT);
    CHECK(raw == nullptr && *m3_last_error());
    CHECK(m3_mindmap_get_outline_json(map.get(), nullptr) == M3_ERR_INVALID_ARGUMENT);
    CHECK(*m3_last_error());
    ok(m3_mindmap_get_outline_json(map.get(), &raw));
    Text owned(raw, m3_string_free);
    CHECK(!*m3_last_error());
    CHECK(Json::parse(owned.get()) == expected);
    CHECK(document(map) == before);

    ok(m3_mindmap_update_node(map.get(), "r", R"({"topic":"After snapshot"})"));
    ok(m3_mindmap_get_outline_json(map.get(), &raw));
    Text changed(raw, m3_string_free);
    auto updated = expected;
    updated["topic"] = "After snapshot";
    CHECK(Json::parse(changed.get()) == updated);
    map.reset();
    CHECK(Json::parse(owned.get()) == expected);

    auto blank = load(Json::object());
    ok(m3_mindmap_get_outline_json(blank.get(), &raw));
    Text blank_text(raw, m3_string_free);
    const auto root = document(blank)["rootId"];
    CHECK(Json::parse(blank_text.get()) == Json({{"id", root}, {"topic", ""}, {"children", Json::array()}}));
}

static Text markdown_text(const Map &map) {
    char *raw = nullptr;
    const auto status = m3_mindmap_to_markdown(map.get(), &raw);
    Text text(raw, m3_string_free);
    ok(status);
    CHECK(text);
    return text;
}

static size_t markdown_occurrences(const std::string &text, const std::string &needle) {
    size_t count = 0, position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

static void markdown() {
    auto minimal = load(Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[
      {"id":"r","topic":"Roadmap","children":["a"]},
      {"id":"a","topic":"Build 世界","note":"line one\nline two"}]})"));
    auto minimal_text = markdown_text(minimal);
    CHECK(std::string(minimal_text.get()) ==
        "# <a id=\"m3-node-72\"></a>Roadmap\n\n"
        "## <a id=\"m3-node-61\"></a>Build 世界\n\n"
        "**Note:** line one<br>line two\n");
    auto blank = load(Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[{"id":"r"}]})"));
    auto blank_text = markdown_text(blank);
    CHECK(std::string(blank_text.get()) == "# <a id=\"m3-node-72\"></a>(untitled)\n");

    auto input = fixture();
    for (auto &n : input["nodes"]) {
        if (n["id"] == "a") n["expanded"] = false;
        if (n["id"] == "r") {
            n["icons"].push_back("star");
            n["style"]["exportSentinel"] = "visual-only-node";
        }
    }
    input["crossLinks"][0]["style"] = {{"exportSentinel", "visual-only-link"}};
    auto map = load(input);
    const auto before = document(map);

    char *invalid = reinterpret_cast<char *>(1);
    CHECK(m3_mindmap_to_markdown(nullptr, &invalid) == M3_ERR_INVALID_ARGUMENT);
    CHECK(invalid == nullptr && *m3_last_error());
    CHECK(m3_mindmap_to_markdown(map.get(), nullptr) == M3_ERR_INVALID_ARGUMENT);
    CHECK(*m3_last_error());
    auto owned = markdown_text(map);
    CHECK(!*m3_last_error());
    const std::string text = owned.get();
    CHECK(document(map) == before);
    CHECK(text.rfind("# <a id=\"m3-node-72\"></a>Racine 世界 🌍\n\n", 0) == 0);
    CHECK(text.back() == '\n' && text[text.size() - 2] != '\n');
    CHECK(text.find('\r') == std::string::npos);
    CHECK(markdown_occurrences(text, "<a id=\"") == 5);
    size_t position = 0;
    for (const char *heading : {
        "# <a id=\"m3-node-72\"></a>Racine 世界 🌍\n\n",
        "## <a id=\"m3-node-61\"></a>(untitled)\n\n",
        "### <a id=\"m3-node-64\"></a>(untitled)\n\n",
        "## <a id=\"m3-node-62\"></a>(untitled)\n\n",
        "## <a id=\"m3-node-63\"></a>(untitled)\n\n"}) {
        const auto found = text.find(heading, position);
        CHECK(found != std::string::npos);
        position = found + std::string(heading).size();
    }
    const std::string metadata =
        "**Note:** Note café\n\n"
        "**URL:** [opaque:anything](<opaque:anything>)\n\n"
        "**Tag:** x\n\n**Tag:** y\n\n**Tag:** x\n\n"
        "**Icon:** star\n\n**Icon:** flag\n\n**Icon:** star\n\n"
        "**Image URL:** [memory:絵](<memory:%E7%B5%B5>)\n\n"
        "![Image](<memory:%E7%B5%B5>)\n\n"
        "**Image size:** " + Json(0.0).dump() + " × " + Json(12.0).dump() + "\n\n";
    CHECK(text.find(metadata + "## <a id=\"m3-node-61\">") != std::string::npos);
    CHECK(text.find("visual-only-node") == std::string::npos);
    CHECK(text.find("visual-only-link") == std::string::npos);
    const std::string section = "---\n\n## Cross-links\n\n";
    CHECK(text.compare(position, section.size(), section) == 0);
    position += section.size();
    for (const char *entry : {
        "- [(untitled)](#m3-node-61) → [(untitled)](#m3-node-62)\n\n"
        "    **ID:** l1\n\n    **Label:** Related\n\n    **Icon:** reference\n\n",
        "- [(untitled)](#m3-node-64) ↔ [(untitled)](#m3-node-63)\n\n"
        "    **ID:** l2\n\n    **Label:** Other\n\n    **Icon:** pin\n\n",
        "- [(untitled)](#m3-node-61) ↔ [(untitled)](#m3-node-61)\n\n"
        "    **ID:** l3\n\n",
        "- [(untitled)](#m3-node-62) → [(untitled)](#m3-node-63)\n\n"
        "    **ID:** l4\n"}) {
        const std::string expected = entry;
        CHECK(text.compare(position, expected.size(), expected) == 0);
        position += expected.size();
    }
    CHECK(position == text.size());

    ok(m3_mindmap_update_node(map.get(), "r", R"({"topic":"After snapshot"})"));
    auto changed = markdown_text(map);
    CHECK(std::string(changed.get()).rfind("# <a id=\"m3-node-72\"></a>After snapshot\n", 0) == 0);
    CHECK(std::string(owned.get()) == text);
    map.reset();
    CHECK(std::string(owned.get()) == text);
}

static void markdown_edges() {
    const std::string literal = "  世界 &copy; <br> > " R"(\`*_{}[]()#+-.!|~)"
        "\r\nCR\rLF\nTAB\t\x01\x1f\x7f" "  ";
    const std::string escaped = "  世界 &amp;copy; &lt;br&gt; &gt; "
        R"(\\\`\*\_\{\}\[\]\(\)\#\+\-\.\!\|\~)"
        "<br>CR<br>LF<br>TAB&#9;&#1;&#31;&#127;  ";
    const std::string root_id = "r\"<&🌍";
    const std::string root_anchor = "m3-node-72223c26f09f8c8d";
    const std::string deep_id = "葉.[x]";
    const std::string deep_anchor = "m3-node-e891892e5b785d";
    const std::string url = "../a b?x=&copy;&q=<\"\\世界>\t\r\n\x1f\x7f%20(keep)";
    const std::string url_text = R"(\.\./a b?x=&amp;copy;&amp;q=&lt;"\\世界&gt;)"
        "&#9;<br>&#31;&#127;%20\\(keep\\)";
    const std::string destination =
        "../a%20b?x=&amp;copy;&amp;q=%3C%22%5C%E4%B8%96%E7%95%8C%3E%09%0D%0A%1F%7F%20(keep)";
    Json nodes = Json::array({{{"id", root_id}, {"topic", literal}, {"note", literal},
        {"children", {"n1", "a"}}}});
    for (size_t depth = 1; depth <= 5; ++depth) {
        nodes.push_back({{"id", "n" + std::to_string(depth)}, {"topic", "D" + std::to_string(depth)},
            {"children", {"n" + std::to_string(depth + 1)}}});
    }
    nodes.push_back({{"id", "n6"}, {"topic", ""}, {"note", literal}, {"hyperLink", url},
        {"tags", {literal, "", literal}}, {"icons", {literal, "", literal}},
        {"image", {{"url", ""}, {"width", 0}, {"height", 0}}}, {"children", {"n7", "s7"}}});
    nodes.push_back({{"id", "n7"}, {"topic", "D7"}, {"note", "seventh"},
        {"image", {{"url", url}, {"width", 1.5}, {"height", 2.25}}}, {"children", {deep_id}}});
    nodes.push_back({{"id", deep_id}, {"topic", "Deep 世界"}, {"note", "eighth"}});
    nodes.push_back({{"id", "s7"}, {"topic", "Sibling"}, {"note", "sibling"}});
    nodes.push_back({{"id", "a"}, {"topic", literal}, {"note", "back at heading"}});
    // The first two sorted links share endpoints; the remaining links close a cycle.
    const Json cross_links = Json::array({
        {{"id", "z"}, {"source", deep_id}, {"target", root_id}, {"directed", true}},
        {{"id", "b"}, {"source", "n6"}, {"target", deep_id}, {"directed", true}},
        {{"id", "a" + literal}, {"source", root_id}, {"target", "n6"}, {"directed", false},
            {"topic", literal}, {"icon", literal}},
        {{"id", "A"}, {"source", root_id}, {"target", "n6"}, {"directed", true}}});
    auto map = load({{"schemaVersion", 1}, {"rootId", root_id}, {"nodes", nodes}, {"crossLinks", cross_links}});
    const auto before = document(map);
    auto owned = markdown_text(map);
    const std::string text = owned.get();
    CHECK(document(map) == before);
    CHECK(text.rfind("# <a id=\"" + root_anchor + "\"></a>" + escaped + "\n\n**Note:** " + escaped + "\n\n", 0) == 0);
    CHECK(markdown_occurrences(text, "<a id=\"") == nodes.size());
    CHECK(markdown_occurrences(text, "<a id=\"" + root_anchor + "\">") == 1);
    CHECK(markdown_occurrences(text, "<a id=\"m3-node-61\">") == 1);
    CHECK(text.find("#######") == std::string::npos);
    CHECK(text.find('\r') == std::string::npos && text.find('\t') == std::string::npos);
    CHECK(text.find("\n \n") == std::string::npos && text.find("\n    \n") == std::string::npos);
    CHECK(text.find("## <a id=\"m3-node-6e31\"></a>D1\n\n"
        "### <a id=\"m3-node-6e32\"></a>D2\n\n"
        "#### <a id=\"m3-node-6e33\"></a>D3\n\n"
        "##### <a id=\"m3-node-6e34\"></a>D4\n\n"
        "###### <a id=\"m3-node-6e35\"></a>D5\n\n"
        "- <a id=\"m3-node-6e36\"></a>(untitled)\n\n") != std::string::npos);
    const std::string sixth =
        "- <a id=\"m3-node-6e36\"></a>(untitled)\n\n"
        "    **Note:** " + escaped + "\n\n"
        "    **URL:** [" + url_text + "](<" + destination + ">)\n\n"
        "    **Tag:** " + escaped + "\n\n    **Tag:** (empty)\n\n    **Tag:** " + escaped + "\n\n"
        "    **Icon:** " + escaped + "\n\n    **Icon:** (empty)\n\n    **Icon:** " + escaped + "\n\n"
        "    **Image URL:** (empty)\n\n"
        "    **Image size:** " + Json(0.0).dump() + " × " + Json(0.0).dump() + "\n\n"
        "    - <a id=\"m3-node-6e37\"></a>D7\n\n";
    CHECK(text.find(sixth) != std::string::npos);
    const std::string seventh =
        "    - <a id=\"m3-node-6e37\"></a>D7\n\n        **Note:** seventh\n\n"
        "        **Image URL:** [" + url_text + "](<" + destination + ">)\n\n"
        "        ![Image](<" + destination + ">)\n\n"
        "        **Image size:** " + Json(1.5).dump() + " × " + Json(2.25).dump() + "\n\n"
        "        - <a id=\"" + deep_anchor + "\"></a>Deep 世界\n\n";
    CHECK(text.find(seventh) != std::string::npos);
    CHECK(text.find("        - <a id=\"" + deep_anchor + "\"></a>Deep 世界\n\n"
        "            **Note:** eighth\n\n"
        "    - <a id=\"m3-node-7337\"></a>Sibling\n\n"
        "        **Note:** sibling\n\n"
        "## <a id=\"m3-node-61\"></a>" + escaped + "\n\n"
        "**Note:** back at heading\n\n---\n\n## Cross-links\n\n") != std::string::npos);
    const auto links_at = text.find("---\n\n## Cross-links\n\n");
    CHECK(links_at != std::string::npos);
    const std::string expected_links =
        "---\n\n## Cross-links\n\n"
        "- [" + escaped + "](#" + root_anchor + ") → [(untitled)](#m3-node-6e36)\n\n"
        "    **ID:** A\n\n"
        "- [" + escaped + "](#" + root_anchor + ") ↔ [(untitled)](#m3-node-6e36)\n\n"
        "    **ID:** a" + escaped + "\n\n    **Label:** " + escaped + "\n\n    **Icon:** " + escaped + "\n\n"
        "- [(untitled)](#m3-node-6e36) → [Deep 世界](#" + deep_anchor + ")\n\n"
        "    **ID:** b\n\n"
        "- [Deep 世界](#" + deep_anchor + ") → [" + escaped + "](#" + root_anchor + ")\n\n"
        "    **ID:** z\n";
    CHECK(text.substr(links_at) == expected_links);

    constexpr size_t count = 1024;
    Json chain_nodes = Json::array();
    for (size_t i = 0; i < count; ++i) {
        Json current = {{"id", "n" + std::to_string(i)}, {"topic", "Chain " + std::to_string(i)}};
        if (i + 1 < count) current["children"] = {"n" + std::to_string(i + 1)};
        chain_nodes.push_back(std::move(current));
    }
    auto chain = load({{"schemaVersion", 1}, {"rootId", "n0"}, {"nodes", chain_nodes},
        {"crossLinks", Json::array({{{"id", "back"}, {"source", "n1023"}, {"target", "n0"}, {"directed", true}}})}});
    auto chain_owned = markdown_text(chain);
    const std::string chain_text = chain_owned.get();
    CHECK(markdown_occurrences(chain_text, "<a id=\"") == count);
    CHECK(chain_text.rfind("# <a id=\"m3-node-6e30\"></a>Chain 0\n\n", 0) == 0);
    const std::string deepest = "\n" + std::string((count - 1 - 6) * 4, ' ') +
        "- <a id=\"m3-node-6e31303233\"></a>Chain 1023\n\n"
        "---\n\n## Cross-links\n\n"
        "- [Chain 1023](#m3-node-6e31303233) → [Chain 0](#m3-node-6e30)\n\n"
        "    **ID:** back\n";
    const auto deepest_at = chain_text.find(deepest);
    CHECK(deepest_at != std::string::npos && deepest_at + deepest.size() == chain_text.size());
}

static void validation() {
    int failures = 0;
    auto reject = [&](const std::string &text, M3Status expected, const char *label) {
        M3Mindmap *p = reinterpret_cast<M3Mindmap *>(1);
        auto status = m3_mindmap_from_json(text.c_str(), &p);
        if (status != expected || p || !*m3_last_error()) {
            std::cerr << label << ": expected " << expected << ", got " << status << '\n';
            ++failures;
        }
        if (status == M3_OK) m3_mindmap_destroy(p);
    };
    reject("{", M3_ERR_JSON, "malformed");
    reject(std::string("{\"x\":\"") + char(0xff) + "\"}", M3_ERR_JSON, "invalid UTF8 JSON");
    reject(R"({"schemaVersion":1,"rootId":"r","nodes":[{"id":"r","id":"r"}]})", M3_ERR_JSON, "duplicate member");
    reject(R"({"schemaVersion":1,"rootId":"r","nodes":[{"id":"r","style":{"nested":{"x":1,"x":2}}}]})", M3_ERR_JSON, "duplicate style member");
    auto bad = [&](auto change, const char *label) { auto j = fixture(); change(j); reject(j.dump(), M3_ERR_SCHEMA, label); };
    bad([](Json &j) { j["schemaVersion"] = 2; }, "future version");
    bad([](Json &j) { j["schemaVersion"] = 1.0; }, "noninteger version");
    bad([](Json &j) { j["nodes"][0]["unknown"] = true; }, "unknown node key");
    bad([](Json &j) { j["extra"] = true; }, "unknown document key");
    bad([](Json &j) { j["nodes"][0]["expanded"] = 1; }, "expanded type");
    bad([](Json &j) { j["nodes"][0]["image"]["width"] = -1; }, "negative image dimension");
    bad([](Json &j) { j["nodes"][0]["image"]["extra"] = 0; }, "unknown image key");
    bad([](Json &j) { j["nodes"][0]["style"] = Json::array(); }, "style type");
    bad([](Json &j) { j["nodes"].push_back(j["nodes"][0]); }, "duplicate node ID");
    bad([](Json &j) { j["crossLinks"].push_back(j["crossLinks"][0]); }, "duplicate link ID");
    bad([](Json &j) { j["nodes"][0]["children"].push_back("missing"); }, "dangling child");
    bad([](Json &j) { j["crossLinks"][0]["source"] = "missing"; }, "dangling link");
    bad([](Json &j) { j["crossLinks"][0]["directed"] = 1; }, "directed type");
    bad([](Json &j) { j["crossLinks"][0]["extra"] = true; }, "unknown link key");
    bad([](Json &j) { j["nodes"][3]["children"] = {"d"}; }, "second parent");
    bad([](Json &j) { j["nodes"][2]["children"] = {"r"}; }, "hierarchy cycle");
    bad([](Json &j) { j["nodes"].push_back({{"id","island"}}); }, "disconnected");
    bad([](Json &j) { j["nodes"][0]["children"].push_back("a"); }, "duplicate child");
    bad([](Json &j) { j["rootId"] = "absent"; }, "missing root");
    bad([](Json &j) { j["nodes"] = Json::array(); }, "empty nodes");
    bad([](Json &j) { j["nodes"][0]["topic"] = std::string("a\0b",3); }, "NUL string");
    bad([](Json &j) { j["nodes"][0]["style"][std::string("a\0b",3)] = 1; }, "NUL key");
    for (const std::string &id : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82")}) {
        M3Mindmap *p = nullptr;
        auto status = m3_mindmap_create(id.c_str(), "", &p);
        if (status != M3_ERR_INVALID_ARGUMENT || p || !*m3_last_error()) ++failures;
        if (status == M3_OK) m3_mindmap_destroy(p);
    }
    CHECK(failures == 0);
    M3Mindmap *p = reinterpret_cast<M3Mindmap *>(1);
    CHECK(m3_mindmap_create(nullptr, "", &p) == M3_ERR_INVALID_ARGUMENT);
    CHECK(p == nullptr);
    CHECK(m3_mindmap_create("r", "", nullptr) == M3_ERR_INVALID_ARGUMENT);
    char *text = reinterpret_cast<char *>(1);
    CHECK(m3_mindmap_to_json(nullptr, &text) == M3_ERR_INVALID_ARGUMENT);
    CHECK(text == nullptr);
    std::string diagnostic = m3_last_error();
    m3_string_free(nullptr); m3_mindmap_destroy(nullptr);
    CHECK(diagnostic == m3_last_error());
    ok(m3_mindmap_create("r", "", &p));
    CHECK(!*m3_last_error());
    m3_mindmap_destroy(p);
}


static void mind_elixir() {
    const auto supplied = Json::parse(R"({"nodeData":{"id":"53f7ebf31cde25df","topic":"VNote","root":true,"children":[{"topic":"haha","id":"53f7ee14ad5cfe73","direction":0},{"topic":"very ok","id":"53f814646ada783d","children":[{"topic":"haha","id":"54c54ce77c9fb334","style":{"color":"#2980b9"}},{"topic":"new node","id":"57fe23a342aa9934","children":[{"topic":"new node","id":"57fe23f7c15f111a","memo":"Very interesting question here is about the samkkdjfkdljfkjdf","hyperLink":"https://bing.com"},{"topic":"new node","id":"57fe25185cc996fe","children":[{"topic":"new node","id":"57fe25b07a7a6eef"},{"topic":"new node","id":"57fe263af1d4a0ce"}]}]}],"direction":1,"style":{"color":"#c0392c","background":"#f39c11"}},{"topic":"a very simple question","id":"54d500c5bf4fb1b3","direction":0}],"style":{"color":"#ecf0f1","background":"#3298db"}},"linkData":{},"direction":2})");
    auto map = load(supplied);
    auto exported = document(map);
    CHECK(exported["schemaVersion"] == 1);
    CHECK(exported["rootId"] == "53f7ebf31cde25df");
    const auto ids = Json::parse(R"(["53f7ebf31cde25df","53f7ee14ad5cfe73","53f814646ada783d","54c54ce77c9fb334","57fe23a342aa9934","57fe23f7c15f111a","57fe25185cc996fe","57fe25b07a7a6eef","57fe263af1d4a0ce","54d500c5bf4fb1b3"])");
    const auto children = Json::parse(R"([["53f7ee14ad5cfe73","53f814646ada783d","54d500c5bf4fb1b3"],[],["54c54ce77c9fb334","57fe23a342aa9934"],[],["57fe23f7c15f111a","57fe25185cc996fe"],[],["57fe25b07a7a6eef","57fe263af1d4a0ce"],[],[],[]])");
    CHECK(exported["nodes"].size() == ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto &n = exported["nodes"][i];
        CHECK(n["id"] == ids[i]);
        CHECK(n["children"] == children[i]);
        CHECK(!n.contains("memo") && !n.contains("direction") && !n.contains("root"));
    }
    CHECK(node(map, "53f7ebf31cde25df")["topic"] == "VNote");
    CHECK(node(map, "53f7ebf31cde25df")["style"] == Json::parse(R"({"color":"#ecf0f1","background":"#3298db"})"));
    CHECK(node(map, "53f814646ada783d")["style"] == Json::parse(R"({"color":"#c0392c","background":"#f39c11"})"));
    CHECK(node(map, "54c54ce77c9fb334")["style"] == Json::parse(R"({"color":"#2980b9"})"));
    CHECK(node(map, "57fe23f7c15f111a")["note"] == "Very interesting question here is about the samkkdjfkdljfkjdf");
    CHECK(node(map, "57fe23f7c15f111a")["hyperLink"] == "https://bing.com");
    CHECK(exported["crossLinks"] == Json::array());
    CHECK(!exported.contains("nodeData") && !exported.contains("linkData") && !exported.contains("direction"));
    auto copy = load(exported);
    CHECK(document(copy) == exported);

    const auto rich_input = Json::parse(R"({"nodeData":{"id":"r","topic":"根 🌍","root":[],"direction":{},
      "children":[{"id":"a","topic":"Café 世界","memo":"Mémo 絵 🌍","expanded":false,
        "tags":["x","y","x"],"icons":["star","flag","star"],"hyperLink":"opaque:世界",
        "style":{"nested":{"list":[1,true,"é",{"direction":7}]}},
        "note":false,"image":17,"parent":["ignored"],"direction":null,"metadata":{"extra":true},
        "children":[{"id":"b","topic":"Hidden","note":"not memo",
          "image":{"url":"memory:絵","width":10,"height":20},"children":[]}]},
        {"id":"c","topic":"Last"}]},"direction":false,"metadata":[1,2],"extension":null,
      "linkData":{
        "l1":{"id":"l1","from":"a","to":"b","label":"related","delta1":{"x":1,"y":2},"delta2":{"x":3,"y":4},
          "directed":false,"style":["ignored"],"icon":{},"topic":"ignored","extension":true},
        "l2":{"id":"l2","from":"a","to":"b","label":"並列"},
        "r":{"id":"r","from":"r","to":"r"}}})");
    auto rich = load(rich_input);
    const auto rich_doc = document(rich);
    CHECK(rich_doc["rootId"] == "r" && rich_doc["schemaVersion"] == 1);
    CHECK(rich_doc["nodes"].size() == 4);
    CHECK(rich_doc["nodes"][0]["id"] == "r" && rich_doc["nodes"][1]["id"] == "a");
    CHECK(rich_doc["nodes"][2]["id"] == "b" && rich_doc["nodes"][3]["id"] == "c");
    CHECK(node(rich, "r")["topic"] == "根 🌍");
    CHECK(node(rich, "r")["children"] == Json({"a","c"}));
    CHECK(node(rich, "a") == Json::parse(R"({"id":"a","topic":"Café 世界","note":"Mémo 絵 🌍",
      "expanded":false,"tags":["x","y","x"],"icons":["star","flag","star"],"hyperLink":"opaque:世界",
      "style":{"nested":{"list":[1,true,"é",{"direction":7}]}},"image":null,"children":["b"]})"));
    CHECK(node(rich, "b")["note"] == "" && node(rich, "b")["image"].is_null());
    CHECK(node(rich, "b")["children"] == Json::array());
    CHECK(rich_doc["crossLinks"].size() == 3);
    CHECK(link(rich, "l1") == Json::parse(R"({"id":"l1","source":"a","target":"b","directed":true,"topic":"related","icon":"","style":{}})"));
    CHECK(link(rich, "l2") == Json::parse(R"({"id":"l2","source":"a","target":"b","directed":true,"topic":"並列","icon":"","style":{}})"));
    CHECK(link(rich, "r") == Json::parse(R"({"id":"r","source":"r","target":"r","directed":true,"topic":"","icon":"","style":{}})"));
    for (const auto *key : {"nodeData", "linkData", "direction", "metadata", "extension"}) CHECK(!rich_doc.contains(key));
    for (const auto &n : rich_doc["nodes"])
        for (const auto *key : {"memo", "root", "direction", "parent", "metadata"}) CHECK(!n.contains(key));
    auto rich_copy = load(rich_doc);
    CHECK(document(rich_copy) == rich_doc);
    for (const auto *record : {R"({"id":"new","memo":"foreign"})", R"({"id":"new","direction":0})"}) {
        CHECK(m3_mindmap_insert_node(rich.get(), "r", M3_APPEND, record) == M3_ERR_SCHEMA);
        CHECK(document(rich) == rich_doc);
    }
    CHECK(m3_mindmap_update_node(rich.get(), "a", R"({"memo":"foreign"})") == M3_ERR_SCHEMA);
    CHECK(m3_mindmap_add_link(rich.get(), R"({"id":"new","from":"a","to":"b"})") == M3_ERR_SCHEMA);
    CHECK(m3_mindmap_update_link(rich.get(), "l1", R"({"label":"foreign"})") == M3_ERR_SCHEMA);
    CHECK(document(rich) == rich_doc);

    auto minimal = load(Json::parse(R"({"nodeData":{"id":"r","topic":""}})"));
    CHECK(document(minimal) == Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[
      {"id":"r","topic":"","note":"","hyperLink":"","expanded":true,"style":{},"tags":[],"icons":[],"image":null,"children":[]}],"crossLinks":[]})"));
    constexpr size_t count = 4096;
    std::string deep = "{\"nodeData\":";
    for (size_t i = 0; i < count; ++i) {
        deep += "{\"id\":\"n" + std::to_string(i) + "\",\"topic\":\"Node\"";
        if (i + 1 < count) deep += ",\"children\":[";
    }
    deep += '}';
    for (size_t i = 1; i < count; ++i) deep += "]}";
    deep += '}';
    M3Mindmap *raw = nullptr;
    ok(m3_mindmap_from_json(deep.c_str(), &raw));
    Map deep_map(raw, m3_mindmap_destroy);
    const auto deep_doc = document(deep_map);
    CHECK(deep_doc["rootId"] == "n0");
    CHECK(deep_doc["nodes"].size() == count);
    for (size_t i = 0; i < count; ++i) {
        const auto &n = deep_doc["nodes"][i];
        CHECK(n["id"] == "n" + std::to_string(i));
        CHECK(n["topic"] == "Node");
        CHECK(n["children"] == (i + 1 < count ? Json::array({"n" + std::to_string(i + 1)}) : Json::array()));
    }
}

static void mind_elixir_validation() {
    int failures = 0;
    auto reject = [&](const std::string &text, M3Status expected, const char *label) {
        M3Mindmap *p = reinterpret_cast<M3Mindmap *>(1);
        auto status = m3_mindmap_from_json(text.c_str(), &p);
        if (status != expected || p || !*m3_last_error()) {
            std::cerr << label << ": expected " << expected << ", got " << status << '\n';
            ++failures;
        }
        if (status == M3_OK) m3_mindmap_destroy(p);
    };
    const auto input = Json::parse(R"({"nodeData":{"id":"r","topic":"Root","children":[
      {"id":"a","topic":"Child","children":[{"id":"b","topic":"Leaf"}]}]},
      "linkData":{"l1":{"id":"l1","from":"r","to":"b","label":"Related"}}})");
    auto bad = [&](auto change, const char *label) { auto j = input; change(j); reject(j.dump(), M3_ERR_SCHEMA, label); };
    for (const auto *key : {"schemaVersion", "rootId", "nodes", "crossLinks"})
        bad([&](Json &j) { j[key] = nullptr; }, key);
    bad([](Json &j) { j["nodeData"] = nullptr; }, "null nodeData");
    bad([](Json &j) { j["nodeData"] = Json::array(); }, "array nodeData");
    bad([](Json &j) { j["nodeData"] = 1; }, "scalar nodeData");
    bad([](Json &j) { j["nodeData"]["id"] = ""; }, "empty node ID");
    bad([](Json &j) { j["nodeData"]["id"] = 1; }, "node ID type");
    bad([](Json &j) { j["nodeData"]["topic"] = nullptr; }, "topic type");
    bad([](Json &j) { j["nodeData"]["children"][0]["id"] = "r"; }, "reused root ID");
    bad([](Json &j) { j["nodeData"]["children"][0]["children"][0]["id"] = "a"; }, "duplicate descendant ID");
    bad([](Json &j) { j["nodeData"]["children"] = nullptr; }, "null children");
    bad([](Json &j) { j["nodeData"]["children"] = 1; }, "scalar children");
    bad([](Json &j) { j["nodeData"]["children"] = Json::object(); }, "object children");
    bad([](Json &j) { j["nodeData"]["children"][0] = "a"; }, "nonobject child");
    bad([](Json &j) { j["nodeData"]["memo"] = nullptr; }, "memo type");
    bad([](Json &j) { j["nodeData"]["hyperLink"] = 1; }, "hyperLink type");
    bad([](Json &j) { j["nodeData"]["expanded"] = 0; }, "expanded type");
    bad([](Json &j) { j["nodeData"]["style"] = Json::array(); }, "style type");
    bad([](Json &j) { j["nodeData"]["tags"] = "x"; }, "tags type");
    bad([](Json &j) { j["nodeData"]["tags"] = Json::array({1}); }, "tag type");
    bad([](Json &j) { j["nodeData"]["icons"] = nullptr; }, "icons type");
    bad([](Json &j) { j["nodeData"]["icons"] = Json::array({false}); }, "icon type");
    bad([](Json &j) { j["linkData"] = nullptr; }, "null linkData");
    bad([](Json &j) { j["linkData"] = Json::array(); }, "array linkData");
    bad([](Json &j) { j["linkData"]["l1"] = 1; }, "nonobject link");
    bad([](Json &j) { j["linkData"]["l1"].erase("id"); }, "missing link ID");
    bad([](Json &j) { j["linkData"]["l1"]["id"] = ""; }, "empty link ID");
    bad([](Json &j) { j["linkData"]["l1"]["id"] = 1; }, "link ID type");
    bad([](Json &j) { j["linkData"]["l1"]["id"] = "l2"; }, "mismatched link ID");
    bad([](Json &j) { j["linkData"][""] = j["linkData"]["l1"]; j["linkData"].erase("l1"); }, "empty link key");
    bad([](Json &j) { j["linkData"]["l1"].erase("from"); }, "missing from");
    bad([](Json &j) { j["linkData"]["l1"].erase("to"); }, "missing to");
    bad([](Json &j) { j["linkData"]["l1"]["from"] = 1; }, "from type");
    bad([](Json &j) { j["linkData"]["l1"]["to"] = nullptr; }, "to type");
    bad([](Json &j) { j["linkData"]["l1"]["from"] = ""; }, "empty from");
    bad([](Json &j) { j["linkData"]["l1"]["to"] = ""; }, "empty to");
    bad([](Json &j) { j["linkData"]["l1"]["from"] = "missing"; }, "dangling from");
    bad([](Json &j) { j["linkData"]["l1"]["to"] = "missing"; }, "dangling to");
    bad([](Json &j) { j["linkData"]["l1"]["label"] = false; }, "label type");
    reject(R"({"nodeData":)", M3_ERR_JSON, "malformed foreign JSON");
    reject(R"({"nodeData":{"id":"r","id":"r","topic":""}})", M3_ERR_JSON, "duplicate member");
    reject(R"({"nodeData":{"id":"r","topic":""},"linkData":{"x":{},"x":{}}})", M3_ERR_JSON, "duplicate link key");
    reject(R"({"nodeData":{"id":"r","topic":""},"extension":{"x":1,"x":2}})", M3_ERR_JSON, "duplicate ignored member");
    reject(std::string(R"({"nodeData":{"id":"r","topic":""},"extension":")") + char(0xff) + "\"}", M3_ERR_JSON, "ignored invalid UTF8");
    bad([](Json &j) { j["extension"] = std::string("a\0b", 3); }, "ignored NUL string");
    bad([](Json &j) { j["extension"][std::string("a\0b", 3)] = 1; }, "ignored NUL key");
    CHECK(failures == 0);
}

static void simple_tree() {
    auto empty = load(Json::object());
    const auto blank = document(empty);
    CHECK(blank["rootId"] == "m3-auto-1");
    CHECK(blank["nodes"].size() == 1 && blank["nodes"][0]["topic"] == "");
    CHECK(blank["nodes"][0]["children"] == Json::array());

    const auto tree = Json::parse(R"({"children":[{},
      {"topic":"Repeated 世界","expanded":false,"children":[{"topic":"Repeated 世界"}]},
      {"id":"m3-auto-1","topic":"Last"}],"extension":{"ignored":true}})");
    for (const auto &input : {tree, Json{{"nodeData", tree}}}) {
        auto map = load(input);
        const auto exported = document(map);
        CHECK(exported["rootId"] == "m3-auto-2");
        CHECK(exported["nodes"].size() == 5);
        CHECK(node(map, "m3-auto-2")["children"] == Json({"m3-auto-3", "m3-auto-4", "m3-auto-1"}));
        CHECK(node(map, "m3-auto-2")["topic"] == "" && node(map, "m3-auto-3")["topic"] == "");
        CHECK(node(map, "m3-auto-4")["children"] == Json({"m3-auto-5"}));
        CHECK(node(map, "m3-auto-4")["expanded"] == false);
        CHECK(node(map, "m3-auto-5")["topic"] == "Repeated 世界");
        CHECK(exported["nodes"][2]["id"] == "m3-auto-4");
        CHECK(exported["nodes"][3]["id"] == "m3-auto-5" && exported["nodes"][4]["id"] == "m3-auto-1");
        auto copy = load(exported);
        CHECK(document(copy) == exported);
        CHECK(document(load(input)) == exported);
        ok(m3_mindmap_update_node(map.get(), "m3-auto-3", R"({"topic":"Edited"})"));
        ok(m3_mindmap_move_node(map.get(), "m3-auto-5", "m3-auto-3", M3_APPEND));
        CHECK(node(map, "m3-auto-3")["topic"] == "Edited");
        CHECK(node(map, "m3-auto-3")["children"] == Json({"m3-auto-5"}));
        CHECK(node(map, "m3-auto-4")["children"] == Json::array());
    }
    auto rich = load(Json::parse(R"({"id":"r","note":"Keep this note","image":{"url":"memory:picture"},
      "hyperLink":"opaque:世界","tags":["one","one"],"icons":["star"],"style":{"custom":[1,true]},
      "children":[{"image":{}},{"image":{"width":12}},{"image":null}]})"));
    CHECK(node(rich, "r")["image"] == Json({{"url","memory:picture"},{"width",0},{"height",0}}));
    CHECK(node(rich, "r")["note"] == "Keep this note");
    CHECK(node(rich, "r")["hyperLink"] == "opaque:世界");
    CHECK(node(rich, "r")["tags"] == Json({"one","one"}));
    CHECK(node(rich, "r")["icons"] == Json({"star"}));
    CHECK(node(rich, "r")["style"] == Json({{"custom",{1,true}}}));
    CHECK(node(rich, "m3-auto-1")["image"] == Json({{"url",""},{"width",0},{"height",0}}));
    CHECK(node(rich, "m3-auto-2")["image"] == Json({{"url",""},{"width",12},{"height",0}}));
    auto rich_copy = load(document(rich));
    CHECK(document(rich_copy) == document(rich));
    auto linked = load(Json::parse(R"({"nodeData":{"children":[{"id":"a"},{"id":"b","memo":"Memo"}]},
      "linkData":{"l":{"id":"l","from":"a","to":"b"}}})"));
    CHECK(link(linked, "l")["source"] == "a" && link(linked, "l")["target"] == "b");
    CHECK(node(linked, "b")["note"] == "Memo");

    for (const auto *invalid : {
        "null", "[]", "42", R"({"id":""})", R"({"id":null})", R"({"topic":1})",
        R"({"children":null})", R"({"children":["leaf"]})", R"({"children":[{},null]})",
        R"({"children":[{"id":"x"},{"children":[{"id":"x"}]}]})",
        R"({"image":{"width":-1}})", R"({"image":{"url":null}})",
        R"({"schemaVersion":1,"children":[{}]})", R"({"rootId":"r"})",
        R"({"nodes":[]})", R"({"crossLinks":[]})", R"({"linkData":{}})",
        R"({"nodeData":{},"schemaVersion":1})",
        R"({"schemaVersion":1,"rootId":"r","nodes":[{"topic":"No ID"}]})"}) {
        M3Mindmap *raw = reinterpret_cast<M3Mindmap *>(1);
        const auto status = m3_mindmap_from_json(invalid, &raw);
        Map unexpected(status == M3_OK ? raw : nullptr, m3_mindmap_destroy);
        CHECK(status == M3_ERR_SCHEMA && raw == nullptr);
    }
    // Optional image fields are import-only: native documents and patches stay strict.
    for (const auto *key : {"url", "width", "height"}) {
        auto native = document(rich);
        native["nodes"][0]["image"].erase(key);
        M3Mindmap *raw = nullptr;
        const auto status = m3_mindmap_from_json(native.dump().c_str(), &raw);
        Map unexpected(raw, m3_mindmap_destroy);
        CHECK(status == M3_ERR_SCHEMA && raw == nullptr);
        auto patch = Json{{"image", node(rich, "r")["image"]}};
        patch["image"].erase(key);
        const auto before = document(rich);
        CHECK(m3_mindmap_update_node(rich.get(), "r", patch.dump().c_str()) == M3_ERR_SCHEMA);
        CHECK(document(rich) == before);
    }
    constexpr size_t count = 4096;
    std::string deep;
    for (size_t i = 1; i < count; ++i) deep += "{\"children\":[";
    deep += "{}";
    for (size_t i = 1; i < count; ++i) deep += "]}";
    M3Mindmap *raw = nullptr;
    ok(m3_mindmap_from_json(deep.c_str(), &raw));
    Map deep_map(raw, m3_mindmap_destroy);
    const auto deep_doc = document(deep_map);
    CHECK(deep_doc["nodes"].size() == count);
    for (size_t i = 0; i < count; ++i) {
        const auto &n = deep_doc["nodes"][i];
        CHECK(n["id"] == "m3-auto-" + std::to_string(i + 1));
        CHECK(n["children"] == (i + 1 < count ? Json::array({"m3-auto-" + std::to_string(i + 2)}) : Json::array()));
    }
}

static void edits() {
    auto map = load(fixture());
    char *old_node = nullptr, *old_doc = nullptr;
    ok(m3_mindmap_get_node_json(map.get(), "r", &old_node));
    ok(m3_mindmap_to_json(map.get(), &old_doc));
    Text owned_node(old_node, m3_string_free), owned_doc(old_doc, m3_string_free);
    auto baseline = document(map);
    ok(m3_mindmap_insert_node(map.get(), "r", 1, R"({"id":"葉🌍","topic":"Leaf","style":{"x":1}})"));
    CHECK(node(map,"r")["children"] == Json({"a","葉🌍","b","c"}));
    ok(m3_mindmap_move_node(map.get(), "葉🌍", "a", 0));
    CHECK(node(map,"r")["children"] == Json({"a","b","c"}));
    CHECK(node(map,"a")["children"] == Json({"葉🌍","d"}));
    CHECK(node(map,"葉🌍")["style"] == Json({{"x",1}}));
    ok(m3_mindmap_move_node(map.get(), "a", "r", 2));
    CHECK(node(map,"r")["children"] == Json({"b","c","a"}));
    ok(m3_mindmap_move_node(map.get(), "b", "r", M3_APPEND));
    CHECK(node(map,"r")["children"] == Json({"c","a","b"}));
    auto root_before = node(map,"r");
    Json patch = {{"topic","New 🌍"},{"style",{{"only",2}}},{"tags",{"t","t"}},
        {"icons",{"one"}},{"hyperLink","nonsense:"},{"image",{{"url","memory:"},{"width",7},{"height",0}}},{"note",""}};
    ok(m3_mindmap_update_node(map.get(), "r", patch.dump().c_str()));
    auto after = node(map,"r");
    for (auto it = patch.begin(); it != patch.end(); ++it) CHECK(after[it.key()] == it.value());
    CHECK(after["expanded"] == root_before["expanded"]);
    CHECK(after["children"] == root_before["children"]);
    CHECK(after["style"] == Json({{"only",2}}));
    ok(m3_mindmap_update_node(map.get(), "r", R"({"image":null})"));
    CHECK(node(map,"r")["image"].is_null());
    auto unchanged = document(map);
    ok(m3_mindmap_update_node(map.get(), "r", "{}"));
    CHECK(document(map) == unchanged);
    CHECK(Json::parse(owned_doc.get()) == baseline);
    CHECK(Json::parse(owned_node.get()) == baseline["nodes"][0]);
    map.reset();
    CHECK(Json::parse(owned_doc.get()) == baseline);
    auto deletion = load(fixture());
    auto survivor = link(deletion,"l4");
    ok(m3_mindmap_remove_subtree(deletion.get(), "a"));
    auto doc = document(deletion);
    CHECK(node(deletion,"r")["children"] == Json({"b","c"}));
    CHECK(doc["nodes"].size() == 3);
    CHECK(doc["nodes"][1]["id"] == "b" && doc["nodes"][2]["id"] == "c");
    CHECK(doc["crossLinks"] == Json::array({survivor}));
}
static void atomicity() {
    auto map = load(fixture());
    const auto baseline = document(map);
    auto fail = [&](M3Status actual, M3Status expected) {
        CHECK(actual == expected); CHECK(*m3_last_error()); CHECK(document(map) == baseline);
    };
    fail(m3_mindmap_move_node(map.get(),"a","d",0), M3_ERR_INVALID_OPERATION);
    fail(m3_mindmap_move_node(map.get(),"a","a",0), M3_ERR_INVALID_OPERATION);
    fail(m3_mindmap_move_node(map.get(),"r","b",0), M3_ERR_INVALID_OPERATION);
    fail(m3_mindmap_remove_subtree(map.get(),"r"), M3_ERR_INVALID_OPERATION);
    fail(m3_mindmap_insert_node(map.get(),"r",0,R"({"id":"a"})"), M3_ERR_ALREADY_EXISTS);
    fail(m3_mindmap_insert_node(map.get(),"r",5,R"({"id":"x"})"), M3_ERR_INVALID_ARGUMENT);
    fail(m3_mindmap_insert_node(map.get(),"r",0,R"({"id":"x","children":["d"]})"), M3_ERR_SCHEMA);
    fail(m3_mindmap_move_node(map.get(),"a","r",3), M3_ERR_INVALID_ARGUMENT);
    fail(m3_mindmap_move_node(map.get(),"a","b",1), M3_ERR_INVALID_ARGUMENT);
    fail(m3_mindmap_update_node(map.get(),"a",R"({"id":"new"})"), M3_ERR_SCHEMA);
    fail(m3_mindmap_update_node(map.get(),"a",R"({"topic":"changed","children":[]})"), M3_ERR_SCHEMA);
    fail(m3_mindmap_update_node(map.get(),"a",R"({"topic":"changed","tags":null})"), M3_ERR_SCHEMA);
    fail(m3_mindmap_update_node(map.get(),"a",R"({"topic":"changed","expanded":1})"), M3_ERR_SCHEMA);
    fail(m3_mindmap_update_link(map.get(),"l1",R"({"topic":"changed","target":"missing"})"), M3_ERR_NOT_FOUND);
    fail(m3_mindmap_update_link(map.get(),"l1",R"({"topic":"changed","directed":1})"), M3_ERR_SCHEMA);
    fail(m3_mindmap_update_link(map.get(),"l1",R"({"topic":"changed","id":"new"})"), M3_ERR_SCHEMA);
    fail(m3_mindmap_add_link(map.get(),R"({"id":"l1","source":"a","target":"b","directed":false})"), M3_ERR_ALREADY_EXISTS);
    fail(m3_mindmap_add_link(map.get(),R"({"id":"new","source":"a","target":"absent","directed":false})"), M3_ERR_NOT_FOUND);
    fail(m3_mindmap_insert_node(map.get(),"absent",0,R"({"id":"x"})"), M3_ERR_NOT_FOUND);
    fail(m3_mindmap_move_node(map.get(),"a","absent",0), M3_ERR_NOT_FOUND);
    fail(m3_mindmap_remove_subtree(map.get(),"absent"), M3_ERR_NOT_FOUND);
    fail(m3_mindmap_update_node(map.get(),"absent","{}"), M3_ERR_NOT_FOUND);
    fail(m3_mindmap_update_link(map.get(),"absent","{}"), M3_ERR_NOT_FOUND);
    fail(m3_mindmap_remove_link(map.get(),"absent"), M3_ERR_NOT_FOUND);
    fail(m3_mindmap_update_node(map.get(),"a",nullptr), M3_ERR_INVALID_ARGUMENT);
    fail(m3_mindmap_move_node(map.get(),"a","\xed\xa0\x80",0), M3_ERR_INVALID_ARGUMENT);
    char *out = reinterpret_cast<char *>(1);
    fail(m3_mindmap_get_node_json(map.get(),"absent",&out), M3_ERR_NOT_FOUND); CHECK(out == nullptr);
    out = reinterpret_cast<char *>(1);
    fail(m3_mindmap_get_link_json(map.get(),"absent",&out), M3_ERR_NOT_FOUND); CHECK(out == nullptr);
}
static void links() {
    auto map = load(fixture());
    const auto before = document(map);
    ok(m3_mindmap_add_link(map.get(),R"({"id":"parallel","source":"a","target":"b","directed":true})"));
    ok(m3_mindmap_add_link(map.get(),R"({"id":"r","source":"b","target":"a","directed":true})"));
    CHECK(document(map)["crossLinks"].size() == 6);
    CHECK(link(map,"l2")["directed"] == false);
    CHECK(link(map,"l3")["source"] == link(map,"l3")["target"]);
    Json change = {{"topic","Edited"},{"icon","arrow"},{"style",{{"new",true}}},{"directed",false},{"source","c"},{"target","a"}};
    ok(m3_mindmap_update_link(map.get(),"l1",change.dump().c_str()));
    auto actual = link(map,"l1");
    for (auto it = change.begin(); it != change.end(); ++it) CHECK(actual[it.key()] == it.value());
    CHECK(actual["id"] == "l1");
    auto copy = load(document(map));
    CHECK(link(copy,"l1") == actual);
    for (const char *id : {"l2","l3","l4"}) {
        auto original = load(fixture());
        CHECK(link(map,id) == link(original,id));
    }
    CHECK(document(map)["nodes"] == before["nodes"]);
    auto all = document(map);
    ok(m3_mindmap_update_link(map.get(),"l1","{}"));
    CHECK(document(map) == all);
    ok(m3_mindmap_remove_link(map.get(),"parallel"));
    CHECK(document(map)["nodes"] == before["nodes"]);
    CHECK(document(map)["crossLinks"].size() == 5);
    CHECK(link(map,"l1") == actual);
}


static void threads() {
    auto map = load(fixture());
    char *missing = nullptr;
    CHECK(m3_mindmap_get_node_json(map.get(),"absent",&missing) == M3_ERR_NOT_FOUND);
    CHECK(missing == nullptr);
    const char *main_borrowed = m3_last_error();
    const std::string main_error = main_borrowed;
    CHECK(!main_error.empty());

    std::promise<void> ready, proceed;
    auto ready_future = ready.get_future();
    auto proceed_future = proceed.get_future();
    auto worker = std::async(std::launch::async,[&] {
        bool signaled = false;
        try {
            CHECK(!*m3_last_error()); // A new thread must not inherit main's error.
            auto independent = load(fixture());
            ok(m3_mindmap_update_node(independent.get(),"r",R"({"topic":"Worker"})"));
            char *snapshot = nullptr;
            ok(m3_mindmap_to_json(independent.get(),&snapshot));
            Text owned(snapshot,m3_string_free);
            M3Mindmap *invalid = reinterpret_cast<M3Mindmap *>(1);
            CHECK(m3_mindmap_from_json("{",&invalid) == M3_ERR_JSON && invalid == nullptr);
            const char *borrowed = m3_last_error();
            const std::string saved = borrowed;
            CHECK(!saved.empty());
            ready.set_value();
            signaled = true;
            proceed_future.wait();
            // Main has completed a successful call; only its error may clear.
            CHECK(saved == borrowed && saved == m3_last_error());
            independent.reset();
            CHECK(saved == m3_last_error());
            CHECK(Json::parse(owned.get())["nodes"][0]["topic"] == "Worker");
        } catch (...) {
            if (!signaled) ready.set_exception(std::current_exception());
            throw;
        }
    });
    ready_future.get(); // Worker failures before the handshake propagate, not deadlock.
    const bool retained = main_error == main_borrowed && main_error == m3_last_error();
    char *snapshot = nullptr;
    const auto status = m3_mindmap_to_json(map.get(),&snapshot);
    const bool cleared = !*m3_last_error();
    Text owned(snapshot,m3_string_free);
    // Release the worker before any throwing assertion in main.
    proceed.set_value();
    worker.get();
    CHECK(retained && status == M3_OK && cleared);
    CHECK(Json::parse(owned.get())["nodes"][0]["topic"] == fixture()["nodes"][0]["topic"]);
}

int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "roundtrip") roundtrip();
        else if (name == "outline") outline();
        else if (name == "markdown") markdown();
        else if (name == "markdown_edges") markdown_edges();
        else if (name == "validation") validation();
        else if (name == "mind_elixir") mind_elixir();
        else if (name == "mind_elixir_validation") mind_elixir_validation();
        else if (name == "simple_tree") simple_tree();
        else if (name == "edits") edits();
        else if (name == "atomicity") atomicity();
        else if (name == "links") links();
        else if (name == "threads") threads();
        else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
