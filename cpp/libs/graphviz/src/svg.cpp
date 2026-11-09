#include "graph.hpp"

#include <memory>
#include <graphviz/gvc.h>

namespace skeleton {
namespace {

struct GraphLayout {
    GVC_t* context;
    Agraph_t* graph;
    ~GraphLayout() { gvFreeLayout(context, graph); }
};

std::string RenderGraph(std::string source) {
    std::unique_ptr<GVC_t, decltype(&gvFreeContext)> context(gvContext(), gvFreeContext);
    if (!context) throw ConfigurationError("cannot create Graphviz context");
    std::unique_ptr<Agraph_t, decltype(&agclose)> graph(agmemread(source.data()), agclose);
    if (!graph || gvLayout(context.get(), graph.get(), "dot") != 0)
        throw ConfigurationError("cannot lay out generated Graphviz graph");
    GraphLayout layout{context.get(), graph.get()};
    char* buffer = nullptr;
    std::size_t length = 0;
    if (gvRenderData(context.get(), graph.get(), "svg", &buffer, &length) != 0)
        throw ConfigurationError("cannot render generated Graphviz graph");
    std::unique_ptr<char, decltype(&gvFreeRenderData)> rendered(buffer, gvFreeRenderData);
    return {rendered.get(), length};
}

}  // namespace

void AddGraphvizImage(GeneratedFiles& files, const std::filesystem::path& stem,
                       const std::string& graph) {
    files[stem.string() + ".dot"] = graph;
    files[stem.string() + ".svg"] = RenderGraph(graph);
}

}  // namespace skeleton
