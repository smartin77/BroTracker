// Compile-only ARM ABI probe. No setup/loop, FILE, test framework or runtime link.
#include "tools/btt/foundation.h"
#include "tools/btt/fs_backend.h"
#include "bringup/working_session.h"
#include "bringup/live_instrument_visit.h"
#include "bringup/pattern_player_selection.h"
#include "bringup/pattern_preview.h"
#include "sd_sample_streaming.h"
#include "sd_pattern_storage.h"
#include "sd_wav_browse_job.h"
#include "sd_wav_audition.h"
#include <cstddef>

using namespace BroTracker;
// Hypothetical bounded handle owners, for layout measurement only. They do not
// implement I/O or immutable-storage policy and are never instantiated or linked.
struct BackingShape final:btt::Reader {
    FsFile file;
    uint32_t length=0,identity_generation=0;
    uint32_t size() const override;
    const void* identity() const override;
    unsigned read(uint32_t,void*,unsigned) const override;
};
struct WriterShape final:btt::Writer {
    FsFile file;
    uint32_t capacity=0;
    const void* identity() const override;
    unsigned write(uint32_t,const void*,unsigned) override;
    bool resize(uint32_t) override;
};
// Probe implementation-dependent make_shared storage without constructing it.
using BackingControl=std::_Sp_counted_ptr_inplace<BackingShape,
    std::allocator<void>,__gnu_cxx::__default_lock_policy>;

#define SIZE(name,type) extern "C" { unsigned char size_##name[sizeof(type)]; }
#define VALUE(name,value) extern "C" { unsigned char value_##name[(value)]; }
SIZE(Document,btt::Document)
SIZE(Workspace,btt::Workspace)
SIZE(Pattern,btt::Pattern)
SIZE(TwoPatterns,btt::Pattern[2])
SIZE(Cell,btt::Cell)
SIZE(Entry,btt::Entry)
SIZE(Span,btt::Span)
SIZE(SharedReader,std::shared_ptr<const btt::Reader>)
SIZE(ReaderOwner,btt::ReaderOwner)
SIZE(Arena,btt::prototype::Arena)
SIZE(FsBackings,btt::prototype::FsBackings)
SIZE(FsOutput,btt::prototype::FsBackings::Output)
SIZE(FsToken,btt::prototype::FsBackings::Token)
SIZE(FsBackend,btt::prototype::FsBackend)
SIZE(Reader,btt::Reader)
SIZE(Writer,btt::Writer)
SIZE(Store,btt::Store)
SIZE(FsFile,FsFile)
SIZE(FatFile,FatFile)
SIZE(ExFatFile,ExFatFile)
SIZE(SdFs,SdFs)
SIZE(SDClass,SDClass)
SIZE(BackingShape,BackingShape)
SIZE(WriterShape,WriterShape)
SIZE(SharedBackingControl,BackingControl)
SIZE(InstrumentDefinition,InstrumentDefinition)
SIZE(InstrumentDefinitions,InstrumentDefinitions)
SIZE(ResourcePathRef,ResourcePathRef)
SIZE(ResourcePathPool,ResourcePathPool)
SIZE(WorkingSession,WorkingSession)
SIZE(WorkingSessionStore,WorkingSessionStore<SdPatternFiles>)
SIZE(PatternFileStore,PatternFileStore<SdPatternFiles>)
SIZE(SdPatternFiles,SdPatternFiles)
SIZE(SdReader,SdReader)
SIZE(SdWriter,SdWriter)
SIZE(SdReadScope,SdReadScope)
SIZE(RealtimePattern,RealtimePattern)
SIZE(PatternPreviewReadback,PatternPreviewReadback)
SIZE(SelectedPatternBringUpControl,SelectedPatternBringUpControl)
SIZE(SelectedPatternBindings,SelectedPatternBindings)
SIZE(LiveInstrumentVisit,LiveInstrumentVisit)
SIZE(SdSampleStreaming,SdSampleStreaming)
SIZE(StreamStorage,SdSampleStreaming::Storage)
SIZE(StreamedSample,StreamedSample)
SIZE(StreamedVoiceBuffer,StreamedVoiceBuffer)
SIZE(StreamedVoiceSnapshot,StreamedVoiceSnapshot)
SIZE(SdWavBrowseJob,SdWavBrowseJob)
SIZE(SdWavAudition,SdWavAudition)
SIZE(AudioBlock,audio_block_t)
VALUE(PatternRowsOffset,offsetof(btt::Pattern,rows))
VALUE(PatternAlignment,alignof(btt::Pattern))
VALUE(WorkspaceAlignment,alignof(btt::Workspace))
VALUE(DocumentAlignment,alignof(btt::Document))
VALUE(StreamRings,sizeof(SdSampleStreaming::Storage::rings))
VALUE(StreamStarts,sizeof(SdSampleStreaming::Storage::starts))
VALUE(StreamStaging,sizeof(SdSampleStreaming::Storage::staging))
VALUE(WorkingSessionCapacity,kWorkingSessionCapacity)
VALUE(PathBytes,kResourcePathBytes)
VALUE(PathEntries,kResourcePathEntries)
static_assert(sizeof(btt::Pattern)==7684,"Retain all three FX slots without per-cell padding");
static_assert(alignof(btt::Pattern)>=alignof(uint32_t),"Natural alignment required");
static_assert(offsetof(btt::Pattern,instruments)%alignof(uint32_t)==0,"Aligned IDs");
static_assert(offsetof(btt::Pattern,fx)%alignof(uint16_t)==0,"Aligned FX");
