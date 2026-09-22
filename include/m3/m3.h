#ifndef M3_H
#define M3_H
#include "m3_layout.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct M3Mindmap M3Mindmap;
#define M3_APPEND ((size_t)-1)
/* All text is NUL-terminated UTF-8. Inputs are borrowed for the call.
 * Output strings are owned snapshots, freed with m3_string_free; they survive
 * edits and model destruction. Output slots must not own live allocations;
 * valid output slots are initialized to NULL even on failure.
 * Independent handles may be used concurrently; synchronize a shared handle.
 * No exception crosses this API. Failed mutations leave the document unchanged.
 * Native JSON is schemaVersion 1, a flat ordered rooted tree plus crossLinks.
 * Unknown native document/record keys and duplicate JSON members are rejected.
 * Styles are opaque objects.
 * Node/link IDs are immutable, case-sensitive, separate namespaces.
 *
 * JSON document schema (text is opaque; no URL fetching or Markdown processing):
 *   {"schemaVersion":1,"rootId":"r","nodes":[{"id":"r"}],"crossLinks":[]}
 * schemaVersion/rootId/nodes are required; crossLinks defaults to []. Every node
 * is reachable from the root by ordered children IDs, with exactly one parent
 * per non-root node. Empty documents, duplicate IDs/children and cycles fail.
 * Node fields: required nonempty id; topic/hyperLink/note default to "";
 * style to {}; expanded to true; tags/icons/children to []; image to null.
 * tags/icons retain string order and duplicates. image is null or an object
 * with required url string and finite nonnegative width/height numbers. Zero
 * image dimensions mean unspecified metadata, not measured layout dimensions.
 * Link fields: required nonempty id/source/target and boolean directed;
 * topic/icon default to "", style to {}. Endpoints must exist. Self-links,
 * parallel links with distinct IDs and cross-link cycles are permitted.
 * Endpoint order is preserved even for undirected links.
 * Styles permit arbitrary nested JSON; other native records reject unknown keys.
 * Decoded NUL characters in keys/values and invalid UTF-8 are not representable.
 * JSON export includes all fields, nodes in root-first child preorder, links in
 * ID byte order. Whitespace, object-key order and numeric spelling are not stable.
 * Geometry never changes the persisted semantic document.
 *
 * Malformed JSON/duplicate members return JSON; schema/type/hierarchy failures
 * return SCHEMA. Invalid direct strings/pointers/indexes/options return
 * INVALID_ARGUMENT; missing IDs/endpoints return NOT_FOUND; duplicate IDs on
 * insertion return ALREADY_EXISTS. Root removal/movement and moves into one's
 * own subtree return INVALID_OPERATION. See m3_last_error for diagnostics. */
M3_API M3Status m3_mindmap_create(const char *root_id, const char *topic, M3Mindmap **out_map);
/* Import also accepts a simple nested node tree, e.g.
 *   {"topic":"Root","children":[{"topic":"Child"},{}]}
 * Every node field is optional: omitted id is generated, topic defaults to "",
 * children to [], and other attributes use native defaults. An empty object is
 * a blank root. Supplied IDs must be nonempty strings and unique; generated
 * m3-auto-N IDs (N starts at 1) follow preorder, skipping all supplied IDs.
 * Simple trees accept native attributes; image url/width/height may also be
 * omitted (""/0/0). Unknown node fields are ignored; supplied known fields must
 * have valid types. Children are nested objects, not IDs. Native document
 * markers schemaVersion/rootId/nodes/crossLinks and envelope markers
 * nodeData/linkData are reserved at the top level; invalid marked documents
 * never fall back to simple-tree decoding.
 *
 * A compatible Mind Elixir v1.1.3 envelope is detected by nodeData (no producer
 * version marker is available). Mixing nodeData
 * with schemaVersion/rootId/nodes/crossLinks is rejected; invalid foreign input
 * never falls back to native decoding. Nested node fields are optional as above;
 * ordered children and supplied IDs are preserved, including collapsed
 * descendants. topic/hyperLink/expanded/style/tags/icons map to native fields;
 * the memo extension maps to note. Optional ID-keyed linkData requires matching
 * record IDs and existing from/to endpoints; label maps to topic, directed is
 * true, and icon/style use native defaults. Unmapped foreign fields are ignored,
 * including direction/root/parent, note/image, link delta1/delta2 and extensions;
 * no source metadata or layout is retained. Parser restrictions still apply to
 * ignored fields. JSON export always uses native schemaVersion 1. Node insertion,
 * node patches and link mutations continue accepting only native records. */
M3_API M3Status m3_mindmap_from_json(const char *json_utf8, M3Mindmap **out_map);
M3_API M3Status m3_mindmap_to_json(const M3Mindmap *map, char **out_json);
/* Lossy nested outline, not native schemaVersion 1 or a full-document export.
 * Each node is {"id":string,"topic":string,"children":[node,...]}.
 * The root is level 1; at most six levels are included. Leaves and level-6
 * nodes have children: []; deeper descendants are not visited or returned.
 * Preserves literal topics (including empty strings) and stored child order,
 * including collapsed descendants within the limit. Omits all other attributes,
 * cross-links and geometry; visibility does not affect the outline.
 * The document is unchanged. Both arguments are required; a non-null output
 * slot is set to NULL even on failure. The independent UTF-8 snapshot survives
 * edits/destruction and must be released with m3_string_free. */
M3_API M3Status m3_mindmap_get_outline_json(const M3Mindmap *map, char **out_json);
/* Markdown text projection, not a round-trip format. Visits every node in stored
 * child order, including collapsed descendants: depths 0-5 use headings H1-H6,
 * deeper nodes use nested lists. Stable ID-derived HTML anchors identify nodes.
 * Includes topics, notes, URLs, ordered tags/icons (including empty/duplicate
 * entries), image URLs/embeddings/dimensions, and ID-sorted cross-links with
 * endpoint topics, direction, IDs, labels and icons. Omits styles, expansion
 * state, layout and geometry. Empty topics display (untitled).
 * Stored Markdown/HTML is escaped as literal text; line breaks become <br>.
 * URL destinations are escaped, not resolved, fetched or scheme-validated.
 * UTF-8 without BOM, structural LF newlines and exactly one final LF; even a
 * blank map produces text. The independent NUL-terminated snapshot survives
 * edits/destruction and must be released with m3_string_free. Both arguments
 * are required; a non-null output slot is set to NULL even on failure. */
M3_API M3Status m3_mindmap_to_markdown(const M3Mindmap *map, char **out_markdown);
M3_API void m3_mindmap_destroy(M3Mindmap *map);
M3_API M3Status m3_mindmap_get_node_json(const M3Mindmap *map, const char *node_id, char **out_json);
/* Insert one leaf at [0, child_count] or M3_APPEND. */
M3_API M3Status m3_mindmap_insert_node(M3Mindmap *map, const char *parent_id, size_t index, const char *node_json);
/* Top-level replacement patch; id/children forbidden. Only image accepts null. */
M3_API M3Status m3_mindmap_update_node(M3Mindmap *map, const char *node_id, const char *patch_json);
/* For same-parent reorder, index is interpreted AFTER removal. Root moves and
 * moves into one's own subtree are invalid operations. */
M3_API M3Status m3_mindmap_move_node(M3Mindmap *map, const char *node_id, const char *new_parent_id, size_t index);
/* Removes descendants and incident links. The root cannot be removed. */
M3_API M3Status m3_mindmap_remove_subtree(M3Mindmap *map, const char *node_id);
M3_API M3Status m3_mindmap_get_link_json(const M3Mindmap *map, const char *link_id, char **out_json);
M3_API M3Status m3_mindmap_add_link(M3Mindmap *map, const char *link_json);
/* Replaces supplied source/target/directed/topic/icon/style; id forbidden. */
M3_API M3Status m3_mindmap_update_link(M3Mindmap *map, const char *link_id, const char *patch_json);
M3_API M3Status m3_mindmap_remove_link(M3Mindmap *map, const char *link_id);
typedef struct M3NodeSize { const char *id; double width, height; } M3NodeSize;
/* One measurement per visible node is required. Valid hidden measurements are
 * accepted; duplicates are invalid and unknown IDs return NOT_FOUND. Collapsed
 * nodes remain visible; their descendants and incident links are omitted only
 * from this result. Returns nodes/treeEdges/crossLinks/bounds, not routed paths. */
M3_API M3Status m3_mindmap_layout_json(const M3Mindmap *map, const M3NodeSize *sizes,
    size_t size_count, const M3LayoutOptions *options, char **out_layout_json);
#ifdef __cplusplus
}
#endif
#endif
