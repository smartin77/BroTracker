#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

// Private writable fixtures, independent of the checkout and launch directory.
class TemporaryTestDirectory {
public:
    TemporaryTestDirectory() {
        static std::atomic<unsigned> sequence{0};
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned attempt=0;attempt<128;++attempt) {
            path_=std::filesystem::temp_directory_path() /
                ("brotracker-test-"+std::to_string(stamp)+"-"+std::to_string(sequence++));
            std::error_code error;
            if(std::filesystem::create_directory(path_,error)) return;
            if(error) throw std::filesystem::filesystem_error("Create test fixture directory",path_,error);
        }
        throw std::runtime_error("Unable to reserve isolated test directory");
    }
    ~TemporaryTestDirectory() { std::error_code error; std::filesystem::remove_all(path_,error); }
    TemporaryTestDirectory(const TemporaryTestDirectory&)=delete;
    TemporaryTestDirectory& operator=(const TemporaryTestDirectory&)=delete;
    const std::filesystem::path& Path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};
