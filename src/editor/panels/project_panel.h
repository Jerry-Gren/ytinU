#pragma once
#include "panel.h"
#include "engine/resource_manager.h"

class ProjectPanel : public Panel {
public:
    ProjectPanel();
    void onImGuiRender() override;
private:
    // 这里可以缓存一些缩略图纹理 ID
};