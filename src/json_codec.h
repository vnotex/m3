#ifndef M3_JSON_CODEC_H
#define M3_JSON_CODEC_H
#include "model.h"
namespace m3 {
Json parse(const char *text);
Attributes patch_attributes(const Attributes &original, const Json &patch);
Link patch_link(const Link &original, const Json &patch);
Node decode_node(const Json &j);
Link decode_link(const Json &j);
Model decode_document(const Json &j);
Json encode_node(const Node &n);
Json encode_link(const Link &l);
Json encode_document(const Model &m);
Json encode_outline(const Model &m);
}
#endif
