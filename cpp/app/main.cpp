#include <iostream>

#include "skeleton/pipeline/generator.hpp"

int main(int argc, char* argv[]) {
    try {
        if (argc == 3 && std::string(argv[1]) == "reset-paths") {
            std::cout << skeleton::RunResetInspection(argv[2]).dump(2) << '\n';
            return 0;
        }
        if (argc == 6 && std::string(argv[1]) == "check-reset") {
            std::cout << skeleton::RunResetValidation(argv[2], argv[3], argv[4], argv[5]).dump(2) << '\n';
            return 0;
        }
        if (argc != 3) {
            std::cerr << "Usage: config-generator SOURCE_DIRECTORY OUTPUT_DIRECTORY\n"
                      << "       config-generator reset-paths CONFIGURATION\n"
                      << "       config-generator check-reset CONFIGURATION SCOPE SNAPSHOT FILESYSTEM\n";
            return 2;
        }
        skeleton::RunGenerationPipeline(argv[1], argv[2]);
        std::cout << "Generated configuration in " << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
