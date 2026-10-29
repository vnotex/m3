#include "test_support.h"
#include "failing_allocator.h"
#include <cstring>

class FailAllocations {
public:
    explicit FailAllocations(size_t after, bool persistent = false) { m3_test_fail_after(after, persistent); }
    ~FailAllocations() { m3_test_stop_failing(); }
    M3AllocationStats stop() { return m3_test_stop_failing(); }
    FailAllocations(const FailAllocations &) = delete;
    FailAllocations &operator=(const FailAllocations &) = delete;
};

// Sweep only paths whose normal cleanup does not allocate. nlohmann/json's
// nonempty-container destructor allocates a work stack (json.hpp:19870), so
// JSON-heavy APIs below inject at entry, not into noexcept dependency cleanup.
// One-shot failure allows unwinding; persistent exhaustion is tested separately
// where there is no candidate object to destroy, isolating error reporting.
template<class T, class Call, class Free>
static void output_failures(const char *label, Call call, Free release, bool sweep = false) {
    constexpr size_t limit = 4096;
    for (size_t after = 0; after < limit; ++after) {
        T *out = reinterpret_cast<T *>(1);
        FailAllocations failure(after);
        const auto status = call(&out);
        const bool diagnostic = *m3_last_error() != '\0';
        const auto stats = failure.stop();
        if (stats.failures) {
            CHECK(stats.failures == 1);
            CHECK(status == M3_ERR_OUT_OF_MEMORY);
            CHECK(out == nullptr && diagnostic);
            // The same public call must recover after the failed allocation.
            ok(call(&out));
            std::unique_ptr<T, Free> recovered(out, release);
            CHECK(recovered && !*m3_last_error());
            if (!sweep) { std::cout << label << ": entry failure and recovery passed\n"; return; }
        } else {
            std::unique_ptr<T, Free> owned(out == reinterpret_cast<T *>(1) ? nullptr : out, release);
            CHECK(status == M3_OK && owned && !diagnostic);
            CHECK(after > 0); // Prove the injector reached real library allocations.
            std::cout << label << ": swept " << after << " allocation failures\n";
            return;
        }
    }
    throw std::runtime_error(std::string(label) + ": allocation sweep did not reach success");
}

template<class T, class Call>
static void schema_failure_unwinding(const char *label, Call call) {
    // A schema exception destroys a nonempty parsed document while unwinding.
    // The injector must leave cleanup allocations alone, not throw a second
    // exception from the dependency's noexcept destructor.
    constexpr const char *invalid = R"({"schemaVersion":1,"rootId":"r","nodes":[{"id":"r"}],"unknown":[]})";
    for (size_t after = 0; after < 4096; ++after) {
        T *out = reinterpret_cast<T *>(1);
        FailAllocations failure(after);
        const auto status = call(invalid,&out);
        const bool diagnostic = *m3_last_error() != '\0';
        const auto stats = failure.stop();
        CHECK(out == nullptr && diagnostic);
        if (stats.failures) {
            CHECK(stats.failures == 1 && status == M3_ERR_OUT_OF_MEMORY);
        } else {
            CHECK(status == M3_ERR_SCHEMA);
            std::cout << label << ": cleanup survived unwinding\n";
            return;
        }
    }
    throw std::runtime_error(std::string(label) + ": schema rejection sweep did not finish");
}

static void outputs() {
    schema_failure_unwinding<M3Mindmap>("schema rejection", m3_mindmap_from_json);
    schema_failure_unwinding<char>("image scanner schema rejection", m3_mindmap_collect_image_references);
    const std::string root_id(96,'r'), topic(192,'t');
    output_failures<M3Mindmap>("create", [&](M3Mindmap **out) {
        return m3_mindmap_create(root_id.c_str(),topic.c_str(),out);
    }, m3_mindmap_destroy, true);
    auto map = load(fixture());
    const auto baseline = document(map);
    const auto encoded = baseline.dump();
    output_failures<M3Mindmap>("import", [&](M3Mindmap **out) {
        return m3_mindmap_from_json(encoded.c_str(),out);
    }, m3_mindmap_destroy);
    output_failures<char>("image scanner", [&](char **out) {
        return m3_mindmap_collect_image_references(encoded.c_str(),out);
    }, m3_string_free);
    CHECK(encoded == baseline.dump());
    output_failures<char>("export", [&](char **out) {
        return m3_mindmap_to_json(map.get(),out);
    }, m3_string_free);
    output_failures<char>("outline snapshot", [&](char **out) {
        return m3_mindmap_get_outline_json(map.get(),out);
    }, m3_string_free);
    output_failures<char>("Markdown export", [&](char **out) {
        return m3_mindmap_to_markdown(map.get(),out);
    }, m3_string_free, true);
    output_failures<char>("node snapshot", [&](char **out) {
        return m3_mindmap_get_node_json(map.get(),"r",out);
    }, m3_string_free);
    output_failures<char>("link snapshot", [&](char **out) {
        return m3_mindmap_get_link_json(map.get(),"l1",out);
    }, m3_string_free);
    const M3LayoutOptions options{M3_LAYOUT_BALANCED,40,20};
    const M3LayoutNode tree[]{{M3_NO_PARENT,100,40},{0,60,20},{0,80,30},{1,40,10}};
    output_failures<M3LayoutResult>("standalone layout", [&](M3LayoutResult **out) {
        return m3_layout_tree(tree,4,&options,out);
    }, m3_layout_result_free, true);
    const M3NodeSize sizes[]{{"r",100,40},{"a",60,20},{"d",40,10},{"b",80,30},{"c",50,10}};
    output_failures<char>("model layout", [&](char **out) {
        return m3_mindmap_layout_json(map.get(),sizes,5,&options,out);
    }, m3_string_free);
    CHECK(document(map) == baseline);

    auto forest_input = baseline;
    forest_input["nodes"][0]["children"] = {"b", "c"};
    forest_input["orphanIds"] = {"a"};
    auto forest = load(forest_input);
    const auto forest_baseline = document(forest);
    const auto forest_encoded = forest_baseline.dump();
    output_failures<M3Mindmap>("forest import", [&](M3Mindmap **out) {
        return m3_mindmap_from_json(forest_encoded.c_str(),out);
    }, m3_mindmap_destroy);
    output_failures<char>("forest image scanner", [&](char **out) {
        return m3_mindmap_collect_image_references(forest_encoded.c_str(),out);
    }, m3_string_free);
    CHECK(forest_encoded == forest_baseline.dump());
    output_failures<char>("forest snapshot", [&](char **out) {
        return m3_mindmap_to_json(forest.get(),out);
    }, m3_string_free);
    output_failures<char>("forest outline", [&](char **out) {
        return m3_mindmap_get_outline_json(forest.get(),out);
    }, m3_string_free);
    output_failures<char>("forest Markdown", [&](char **out) {
        return m3_mindmap_to_markdown(forest.get(),out);
    }, m3_string_free, true);
    CHECK(document(forest) == forest_baseline);

    {
        char *images = reinterpret_cast<char *>(1);
        FailAllocations exhausted(0, true);
        const auto status = m3_mindmap_collect_image_references(encoded.c_str(), &images);
        const bool diagnostic = *m3_last_error() != '\0';
        m3_string_free(images);
        const auto stats = exhausted.stop();
        CHECK(status == M3_ERR_OUT_OF_MEMORY && images == nullptr && diagnostic);
        CHECK(stats.attempts == 1 && stats.failures == 1);
        ok(m3_mindmap_collect_image_references(encoded.c_str(), &images));
        Text recovered(images, m3_string_free);
        CHECK(recovered && !*m3_last_error());
        CHECK(Json::parse(recovered.get()) == Json::array({"memory:絵"}));
    }

    M3Mindmap *out = reinterpret_cast<M3Mindmap *>(1);
    FailAllocations exhausted(0, true);
    const auto status = m3_mindmap_create("r","Root",&out);
    const bool diagnostic = *m3_last_error() != '\0';
    char saved[256];
    std::strncpy(saved,m3_last_error(),sizeof saved);
    saved[sizeof saved-1] = '\0';
    m3_string_free(nullptr); m3_mindmap_destroy(nullptr); m3_layout_result_free(nullptr);
    const bool retained = std::strcmp(saved,m3_last_error()) == 0;
    const auto stats = exhausted.stop();
    CHECK(status == M3_ERR_OUT_OF_MEMORY && out == nullptr && diagnostic && retained);
    CHECK(stats.attempts == 1 && stats.failures == 1); // No allocation to report/query/free after failure.
}

template<class Call>
static void mutation_failures(const char *label, const Json &input, Call call, bool sweep = false) {
    auto successful = load(input);
    ok(call(successful.get()));
    const auto expected = document(successful);
    for (size_t after = 0; after < 4096; ++after) {
        auto map = load(input);
        const auto baseline = document(map);
        CHECK(expected != baseline); // Every case must exercise a real semantic edit.
        FailAllocations failure(after);
        const auto status = call(map.get());
        const bool diagnostic = *m3_last_error() != '\0';
        const auto stats = failure.stop();
        if (stats.failures) {
            CHECK(stats.failures == 1 && status == M3_ERR_OUT_OF_MEMORY && diagnostic);
            CHECK(document(map) == baseline);
            ok(call(map.get()));
            CHECK(!*m3_last_error());
            CHECK(document(map) == expected);
            if (!sweep) { std::cout << label << ": entry failure and atomic recovery passed\n"; return; }
        } else {
            CHECK(status == M3_OK && !diagnostic);
            CHECK(document(map) == expected);
            std::cout << label << ": swept " << after << " atomic failures\n";
            return;
        }
    }
    throw std::runtime_error(std::string(label) + ": allocation sweep did not reach success");
}

static void mutations() {
    auto input = fixture();
    // Keep semantic relationships/long text but make deletion's JSON cleanup
    // allocation-free; otherwise a destructor failure cannot be caught by C++.
    for (auto &n : input["nodes"]) n["style"] = Json::object();
    for (auto &l : input["crossLinks"]) l["style"] = Json::object();
    const std::string long_id(96,'a'), long_leaf(96,'d'), long_link(96,'l');
    for (auto &n : input["nodes"]) {
        if (n["id"] == "a") n["id"] = long_id;
        if (n["id"] == "d") n["id"] = long_leaf;
        if (n.contains("children")) for (auto &child : n["children"]) {
            if (child == "a") child = long_id;
            if (child == "d") child = long_leaf;
        }
    }
    for (auto &l : input["crossLinks"]) {
        if (l["id"] == "l1") l["id"] = long_link;
        for (const char *endpoint : {"source","target"}) {
            if (l[endpoint] == "a") l[endpoint] = long_id;
            if (l[endpoint] == "d") l[endpoint] = long_leaf;
        }
    }
    mutation_failures("cross-parent move",input,[&](M3Mindmap *m) {
        return m3_mindmap_move_node(m,long_id.c_str(),"b",0);
    },true);
    mutation_failures("same-parent reorder",input,[&](M3Mindmap *m) {
        return m3_mindmap_move_node(m,long_id.c_str(),"r",2);
    },true);
    mutation_failures("subtree removal",input,[&](M3Mindmap *m) {
        return m3_mindmap_remove_subtree(m,long_id.c_str());
    },true);
    mutation_failures("link removal",input,[&](M3Mindmap *m) {
        return m3_mindmap_remove_link(m,long_link.c_str());
    },true);
    mutation_failures("short-ID link removal",input,[](M3Mindmap *m) {
        return m3_mindmap_remove_link(m,"l4");
    },true);
    mutation_failures("insert",input,[](M3Mindmap *m) {
        return m3_mindmap_insert_node(m,"r",1,R"({"id":"new","topic":"Inserted"})");
    });
    mutation_failures("first orphan insert",input,[](M3Mindmap *m) {
        return m3_mindmap_insert_node(m,"",0,R"({"id":"new","topic":"Orphan"})");
    });
    mutation_failures("detach into empty orphan list",input,[&](M3Mindmap *m) {
        return m3_mindmap_move_node(m,long_id.c_str(),"",0);
    },true);
    const std::string long_orphan(96,'o');
    auto forest = input;
    forest["nodes"][0]["children"] = {"b", "c"};
    forest["orphanIds"] = {long_id, long_orphan};
    forest["nodes"].push_back({{"id", long_orphan}});
    mutation_failures("orphan insert",forest,[](M3Mindmap *m) {
        return m3_mindmap_insert_node(m,"",1,R"({"id":"new","topic":"Orphan"})");
    });
    mutation_failures("detach into existing orphan list",forest,[](M3Mindmap *m) {
        return m3_mindmap_move_node(m,"b","",1);
    },true);
    mutation_failures("reattach orphan to main tree",forest,[&](M3Mindmap *m) {
        return m3_mindmap_move_node(m,long_id.c_str(),"r",1);
    },true);
    mutation_failures("reattach orphan to another orphan",forest,[&](M3Mindmap *m) {
        return m3_mindmap_move_node(m,long_id.c_str(),long_orphan.c_str(),0);
    },true);
    mutation_failures("orphan reorder",forest,[&](M3Mindmap *m) {
        return m3_mindmap_move_node(m,long_id.c_str(),"",1);
    },true);
    mutation_failures("orphan subtree removal",forest,[&](M3Mindmap *m) {
        return m3_mindmap_remove_subtree(m,long_id.c_str());
    },true);
    mutation_failures("node update",input,[](M3Mindmap *m) {
        return m3_mindmap_update_node(m,"r",R"({"topic":"Updated","tags":["replacement"],"style":{"nested":[1,2]}})");
    });
    mutation_failures("link add",input,[](M3Mindmap *m) {
        return m3_mindmap_add_link(m,R"({"id":"new","source":"r","target":"b","directed":true,"topic":"Added"})");
    });
    mutation_failures("link update",input,[&](M3Mindmap *m) {
        return m3_mindmap_update_link(m,long_link.c_str(),R"({"topic":"Updated","target":"r","style":{"width":3}})");
    });
}

int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "outputs") outputs();
        else if (name == "mutations") mutations();
        else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
