#include "test_framework.h"
#include <SD.h>
#include "fs_backend.h"
#include <cstdio>

namespace {
using namespace btt;
using namespace btt::prototype;
void Reset() {
    DeviceSd::files.clear();DeviceSd::fail_write=false;DeviceSd::fail_write_path.clear();DeviceSd::fail_close_path.clear();
    DeviceSd::rejected_seeks=0;DeviceSd::rejected_offset=DeviceSd::rejected_size=0;
}
std::string Path(FsBackings::Token t) {
    char path[96];std::snprintf(path,sizeof(path),BTT_BACKING_DIRECTORY "/%08x-%u-%08x.btt",t.domain,t.slot,t.generation);return path;
}
TEST_CASE(SdFatFileContractRejectsWritableSeekAndTruncatePastEof) {
    Reset();FsFile file;CHECK(file.open(BTT_BACKING_DIRECTORY "/seek.btt",O_RDWR|O_CREAT|O_EXCL));
    CHECK(!file.seekSet(32));CHECK_EQ(file.size(),0u);CHECK(!file.truncate(32));CHECK_EQ(file.getError(),0u);
    CHECK(file.close());CHECK_EQ(DeviceSd::rejected_seeks,1u);
}
TEST_CASE(FsBackendFillsWriteGapsAndResizeGrowthFromActualEof) {
    Reset();FsBackend backend(true);FsBackings pool(backend);FsBackings::Output output;FsBackings::Token token;
    CHECK_EQ(pool.Create(output,token),Code::Ok);uint8_t payload[24];for(unsigned i=0;i<24;++i)payload[i]=i+1;
    // Rewrite's first output is a directory entry at 32 into an empty file.
    CHECK_EQ(output.write(32,payload,sizeof(payload)),sizeof(payload));CHECK(output.resize(600));
    auto& bytes=DeviceSd::files[Path(token)].bytes;CHECK_EQ(bytes.size(),600u);
    for(unsigned i=0;i<600;++i)CHECK_EQ(bytes[i],i>=32 && i<56?payload[i-32]:0u);
    const uint8_t header[]={7,8,9};CHECK_EQ(output.write(4,header,3),3u);CHECK(output.resize(48));
    CHECK_EQ(bytes.size(),48u);CHECK_EQ(bytes[4],7u);CHECK_EQ(bytes[32],1u);
    ReaderOwner owner;CHECK_EQ(output.Finish(owner),Code::Ok);CHECK_EQ(owner->size(),48u);
    uint8_t reopened[48];CHECK_EQ(owner->read(0,reopened,sizeof(reopened)),sizeof(reopened));CHECK(!std::memcmp(reopened,bytes.data(),sizeof(reopened)));
    owner.reset();CHECK(pool.CanDestroy());CHECK_EQ(DeviceSd::rejected_seeks,0u);
}
TEST_CASE(FsBackendGrowthFailureRemainsRecordedAfterCheckedCloseWithoutLiveOwner) {
    Reset();FsBackend backend(true);FsBackings pool(backend);FsBackings::Output output;FsBackings::Token token;
    CHECK_EQ(pool.Create(output,token),Code::Ok);DeviceSd::fail_write=true;const uint8_t value=1;
    CHECK_EQ(output.write(32,&value,1),0u);CHECK_EQ(pool.Inspect(token),Code::Io);
    FsBackings::Resources state;CHECK(pool.InspectResources(token,state));CHECK(state.writer);CHECK(state.open);CHECK(state.poisoned);CHECK(!state.quarantined);
    DeviceSd::fail_write=false;CHECK_EQ(output.Cancel(),Code::Ok);
    CHECK(pool.InspectResources(token,state));CHECK_EQ(state.recorded,Code::Io);CHECK_EQ(state.refs,0u);
    CHECK(!state.writer);CHECK(!state.open);CHECK(!state.quarantined);CHECK(state.poisoned);CHECK(pool.CanDestroy());
    CHECK_EQ(pool.Inspect(token),Code::Io); // Snapshot/cleanup must not erase history.
}
}
