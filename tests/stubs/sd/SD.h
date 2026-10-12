#pragma once
// Test-only SD API fake. Compiles the real SdReader/shared lock and firmware adapter.
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>
#include <algorithm>
constexpr int FILE_READ=0, FILE_WRITE=1;
namespace FakeSd {
inline std::vector<std::uint8_t> bytes;
inline bool fail_open=false,directory=false,fail_seek=false,negative_read=false;
inline unsigned opens=0,closes=0,seeks=0;
inline std::size_t short_at=SIZE_MAX;
inline void (*on_read)()=nullptr;
inline void Reset() { fail_open=directory=fail_seek=negative_read=false; opens=closes=seeks=0; short_at=SIZE_MAX; on_read=nullptr; }
}
class File {
    bool open_=false; std::size_t offset_=0;
public:
    File()=default; explicit File(bool opened):open_(opened) {}
    explicit operator bool() const { return open_; }
    void close() { if(open_) ++FakeSd::closes; open_=false; }
    int read(void* out,std::size_t count) {
        if(FakeSd::on_read) FakeSd::on_read();
        if(FakeSd::negative_read) return -1;
        count=std::min(count,FakeSd::bytes.size()-offset_);
        if(offset_>=FakeSd::short_at && count) --count;
        std::copy_n(FakeSd::bytes.data()+offset_,count,static_cast<std::uint8_t*>(out)); offset_+=count; return int(count);
    }
    bool seek(std::uint32_t n) { ++FakeSd::seeks; if(FakeSd::fail_seek || n>FakeSd::bytes.size()) return false; offset_=n; return true; }
    std::uint32_t position() { return offset_; }
    int available() { return int(FakeSd::bytes.size()-offset_); }
    std::uint64_t size() { return FakeSd::bytes.size(); }
    bool isDirectory() { return FakeSd::directory; }
    File openNextFile() { return {}; }
    void flush() {} void print(const char*) {} void println(const char*) {} void println() {}
    std::size_t write(const std::uint8_t*,std::size_t n) { return n; }
};
struct FakeSD {
    File open(const char*,int) { ++FakeSd::opens; return File(!FakeSd::fail_open); }
};
inline FakeSD SD;
