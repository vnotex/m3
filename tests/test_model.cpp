#include "test_support.h"
#include <future>
#include <exception>
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
        else if (name == "validation") validation();
        else if (name == "edits") edits();
        else if (name == "atomicity") atomicity();
        else if (name == "links") links();
        else if (name == "threads") threads();
        else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
