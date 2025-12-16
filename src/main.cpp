#include <cstdlib>
#include <iostream>
#include <filesystem>

#include "scene_roaming.h"

std::string getExecutableDir() {
    return std::filesystem::current_path().string(); 
}

Options getOptions(int argc, char* argv[]) {
    Options options;
    options.windowTitle = "Scene Roaming";
    options.windowWidth = 1920;
    options.windowHeight = 1080;
    options.windowResizable = true;
    options.vSync = true;
    options.msaa = true;
    options.glVersion = {3, 3};
    options.backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    std::string exeDir = getExecutableDir();
    std::filesystem::path assetPath = std::filesystem::path(exeDir) / "media";
    options.assetRootDir = assetPath.string() + "/";

    std::cout << "[Info] Asset Root: " << options.assetRootDir << std::endl;

    return options;
}

int main(int argc, char* argv[]) {
    Options options = getOptions(argc, argv);

    try {
        SceneRoaming app(options);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        exit(EXIT_FAILURE);
    } catch (...) {
        std::cerr << "Unknown exception" << std::endl;
        exit(EXIT_FAILURE);
    }

    return 0;
}