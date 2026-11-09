#pragma once

#include <filesystem>
#include <random>

namespace skeleton::test {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::random_device random;
        path = std::filesystem::temp_directory_path() /
               ("skeleton-test-" + std::to_string(random()) + "-" +
                std::to_string(random()));
        std::filesystem::create_directory(path);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path path;
};


}  // namespace skeleton::test
