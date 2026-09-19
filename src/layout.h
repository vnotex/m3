#ifndef M3_LAYOUT_INTERNAL_H
#define M3_LAYOUT_INTERNAL_H
#include "model.h"
namespace m3 {
struct Geometry { std::vector<M3Rect> rects; M3Rect bounds{}; };
Geometry layout_tree(const M3LayoutNode *nodes, size_t count, const M3LayoutOptions &options);
Json layout_model(const Model &model, const M3NodeSize *sizes, size_t count, const M3LayoutOptions &options);
}
#endif
