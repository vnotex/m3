#include <m3/m3.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CALL(expression) do { M3Status status = (expression); if (status != M3_OK) { \
    fprintf(stderr,"%s: status %d: %s\n",#expression,(int)status,m3_last_error()); goto cleanup; } } while (0)
#define CHECK(expression) do { if (!(expression)) { fprintf(stderr,"Failed: %s\n",#expression); goto cleanup; } } while (0)
static int near(double actual, double expected) { return fabs(actual-expected) < 1e-9; }
int main(void) {
    int result = 1;
    M3Mindmap *map = NULL, *copy = NULL;
    char *document = NULL, *layout = NULL, *link = NULL, *markdown = NULL;
    M3LayoutResult *tree = NULL;
    const M3LayoutOptions options = {M3_LAYOUT_RIGHT,40,20};
    const M3NodeSize sizes[] = {{"r",100,40},{"a",60,20}};
    const M3LayoutNode nodes[] = {{M3_NO_PARENT,100,40},{0,60,20}};
    CALL(m3_mindmap_create("r","Root",&map));
    CALL(m3_mindmap_insert_node(map,"r",M3_APPEND,"{\"id\":\"a\",\"topic\":\"Child 世界 🌍\"}"));
    CALL(m3_mindmap_add_link(map,"{\"id\":\"related\",\"source\":\"r\",\"target\":\"a\",\"directed\":true,\"topic\":\"Related\",\"icon\":\"reference\",\"style\":{\"color\":\"#336699\"}}"));
    CALL(m3_mindmap_update_link(map,"related","{\"topic\":\"Updated\",\"icon\":\"arrow\",\"style\":{\"width\":2},\"directed\":false}"));
    CALL(m3_mindmap_to_markdown(map,&markdown));
    CALL(m3_mindmap_to_json(map,&document));
    puts(document);
    CALL(m3_mindmap_from_json(document,&copy));
    m3_mindmap_destroy(map); map = NULL;
    CHECK(strstr(markdown,"# <a id=\"m3-node-72\"></a>Root\n") != NULL);
    CHECK(strstr(markdown,"## <a id=\"m3-node-61\"></a>Child 世界 🌍\n") != NULL);
    CHECK(strstr(markdown,"- [Root](#m3-node-72) ↔ [Child 世界 🌍](#m3-node-61)") != NULL);
    CHECK(strstr(markdown,"**Label:** Updated") != NULL && strstr(markdown,"**Icon:** arrow") != NULL);
    puts(markdown);
    CALL(m3_mindmap_get_link_json(copy,"related",&link));
    CHECK(strstr(link,"Updated") != NULL && strstr(link,"arrow") != NULL);
    CALL(m3_mindmap_layout_json(copy,sizes,2,&options,&layout));
    puts(layout);
    CALL(m3_layout_tree(nodes,2,&options,&tree));
    CHECK(tree->node_count == 2);
    CHECK(near(tree->rects[0].x,-50) && near(tree->rects[0].y,-20));
    CHECK(near(tree->rects[1].x,90) && near(tree->rects[1].y,-10));
    CHECK(near(tree->bounds.x,-50) && near(tree->bounds.y,-20));
    CHECK(near(tree->bounds.width,200) && near(tree->bounds.height,40));
    puts("m3 C99 application passed");
    result = 0;
cleanup:
    m3_layout_result_free(tree);
    m3_string_free(link);
    m3_string_free(layout);
    m3_string_free(document);
    m3_string_free(markdown);
    m3_mindmap_destroy(copy);
    m3_mindmap_destroy(map);
    return result;
}
