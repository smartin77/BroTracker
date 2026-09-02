#include "storage.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
std::tm CompileTime()
{
    std::tm result{};
    std::istringstream input(std::string(__DATE__) + " " + __TIME__);
    input >> std::get_time(&result, "%b %d %Y %H:%M:%S");
    return result;
}

std::tm InitializationTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp =
        std::chrono::system_clock::to_time_t(now);

    if (timestamp <= 0)
        return CompileTime();

    std::tm result{};
#if defined(_WIN32)
    localtime_s(&result, &timestamp);
#else
    localtime_r(&timestamp, &result);
#endif
    return result;
}
}

namespace BroTracker
{
    bool Storage::Initialize()
    {
        const std::filesystem::path directory = "BroTracker";
        const std::filesystem::path log_path =
            directory / "initialization.log";

        std::error_code error;
        std::filesystem::create_directories(directory, error);

        if (error)
            return false;

        std::ofstream log(log_path, std::ios::trunc);

        if (!log)
            return false;

        const std::tm timestamp = InitializationTime();
        log << "Timestamp: "
            << std::put_time(&timestamp, "%Y-%m-%d %H:%M:%S")
            << '\n'
            << "BroTracker initialized\n";

        return log.good();
    }
}