#ifndef M3_LAYOUT_H
#define M3_LAYOUT_H
#include "m3_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define M3_NO_PARENT ((size_t)-1)
typedef enum M3LayoutDirection {
    M3_LAYOUT_BALANCED = 0, M3_LAYOUT_RIGHT = 1, M3_LAYOUT_LEFT = 2
} M3LayoutDirection;
typedef struct M3Rect { double x, y, width, height; } M3Rect;
typedef struct M3LayoutNode { size_t parent_index; double width, height; } M3LayoutNode;
typedef struct M3LayoutOptions {
    M3LayoutDirection direction;
    double horizontal_gap, vertical_gap;
} M3LayoutOptions;
typedef struct M3LayoutResult {
    size_t node_count;
    const M3Rect *rects;
    M3Rect bounds;
} M3LayoutResult;
/* Borrowed inputs; required options. Positive finite sizes, nonnegative finite
 * gaps. Exactly one root (at any index); siblings retain input order.
 * Empty input succeeds. Rectangles correspond to input indexes, root centered
 * at (0,0). Rectangle coordinates are top-left; bounds are their union without
 * padding. Subtree span is max(node height, stacked child spans plus gaps).
 * Each child stack is centered on its parent's center Y; root side stacks are
 * separately centered at zero. Horizontal gaps separate adjacent generations.
 * BALANCED assigns root branches to the smaller current stacked side span,
 * RIGHT on ties. Branch descendants inherit that side. LEFT reflects RIGHT.
 * Overflow/nonfinite intermediate geometry returns INVALID_ARGUMENT.
 * The output slot is set to NULL on failure and must not own a live result. */
M3_API M3Status m3_layout_tree(const M3LayoutNode *nodes, size_t count,
    const M3LayoutOptions *options, M3LayoutResult **out_result);
/* Frees the immutable snapshot and its rectangles; NULL is accepted. */
M3_API void m3_layout_result_free(M3LayoutResult *result);
#ifdef __cplusplus
}
#endif
#endif
