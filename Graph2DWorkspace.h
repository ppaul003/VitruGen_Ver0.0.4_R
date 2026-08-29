#ifndef VITRUGEN_GRAPH_2D_WORKSPACE_H
#define VITRUGEN_GRAPH_2D_WORKSPACE_H

#include "IWorkspace.h"
#include "TheArbiter.h"

#include <cstdint>
#include <string>
#include <vector>

class Graph2DWorkspace : public IWorkspace {
public:
    enum class Layer1Item {
        Workspace = 0,
        Count
    };

public:
    Graph2DWorkspace();
    ~Graph2DWorkspace() override;

    bool initialize(WorkspaceServices& services) override;
    void enter(WorkspaceServices& services) override;
    void exit(WorkspaceServices& services) override;

    void update(
        const WorkspaceFrameContext& frame,
        WorkspaceServices& services
    ) override;

    void render(
        const WorkspaceFrameContext& frame,
        WorkspaceServices& services
    ) override;

    bool handleInput(
        const WorkspaceInputEvent& input,
        WorkspaceServices& services
    ) override;

    WorkspacePresentation buildPresentation() const override;
    WorkspacePresentation buildLayer1TransitionPresentation() const;

private:
    WorkspacePresentation buildLayer1Presentation() const;

private:
    Layer1Item m_layer1Item = Layer1Item::Workspace;
};

#endif
