#include "test_framework.h"
#include "ui/wav_browser.h"
#include "ui/pattern_screen.h"
#include "firmware/teensy/BroTracker/sd_pattern_storage.h"
#include <array>
using namespace BroTracker;
namespace {
std::vector<std::uint8_t> Wave(unsigned channels=1,unsigned bits=16,unsigned rate=44100,unsigned format=1) {
    const unsigned align=channels*(bits/8),bytes=16*align;
    std::vector<std::uint8_t> b(44+bytes);
    const auto put=[&](unsigned at,unsigned value,unsigned width){for(unsigned i=0;i<width;++i)b[at+i]=value>>(8*i);};
    std::memcpy(b.data(),"RIFF",4);put(4,b.size()-8,4);std::memcpy(b.data()+8,"WAVEfmt ",8);
    put(16,16,4);put(20,format,2);put(22,channels,2);put(24,rate,4);put(28,rate*align,4);put(32,align,2);put(34,bits,2);
    std::memcpy(b.data()+36,"data",4);put(40,bytes,4);
    for(unsigned i=0;i<bytes;++i)b[44+i]=static_cast<std::uint8_t>((i*31+17)&255);
    return b;
}
void Begin(SdPatternFiles& backend) {
    FakePatternSd::Reset();backend.ready=true;FakePatternSd::files["/"].directory=true;
    FakePatternSd::files["/Samples"].directory=true;FakePatternSd::files["/Samples/Nested"].directory=true;
}
void List(SdPatternFiles& backend,WavBrowser& browser,const char* path="/Samples") {
    WavOpenRequest request;request.version=3;std::strcpy(request.directory,path);browser.Begin(path);
    for(unsigned page_index=0;page_index<100;++page_index) {
        WavBrowsePage page;const auto before=FakePatternSd::reads.size();
        CHECK_EQ(ReadWavBrowsePage(backend,request,page),WavDirectoryResult::Ok);
        unsigned files=0;for(unsigned i=0;i<page.count;++i)if(page.entries[i].kind==WavEntryKind::File){++files;CHECK(page.entries[i].facts.known);}
        CHECK(files<=1);CHECK(page.scanned<=64);
        // Simple fixture metadata reads are RIFF(12), fmt header(8), fmt(16), data header(8).
        CHECK(FakePatternSd::reads.size()-before<=4);
        CHECK(browser.Accept(page));if(page.done)return;
        CHECK(page.next>request.offset);request.offset=page.next;
    }
    CHECK(false);
}
void Highlight(WavBrowser& browser,const std::string& name) {
    const auto& view=browser.View();const auto found=std::find(view.names.begin(),view.names.end(),name);CHECK(found!=view.names.end());
    const auto index=static_cast<unsigned>(found-view.names.begin());
    while(browser.View().selected!=index) {
        browser.Move(browser.View().selected<index?1:-1);
        CHECK(browser.View().selected>=browser.View().first);CHECK(browser.View().selected<browser.View().first+browser.View().rows);
    }
}
void Screenshot(const char* name,const WavBrowser& browser,bool confirming=false,const std::string& confirmation={}) {
    HostPanelInformation host;host.sample_picker=true;host.load_save_screen=true;host.browser_directory=browser.Directory();
    host.pattern_browser_list=browser.View();host.pattern_browser_text=confirming?confirmation:browser.Message();host.sample_browser_confirmation=confirming;
    host.diagnostic_status="DIAGNOSTICS MUST BE HIDDEN";host.transport_hints="CONTROL HINTS MUST BE HIDDEN";host.instrument_number=6;
    Tune tune;Pattern pattern;Framebuffer actual(640,480),without_diagnostics(640,480);
    RenderMainScreen(actual,tune,pattern,std::nullopt,nullptr,host);
    host.diagnostic_status.clear();host.transport_hints.clear();RenderMainScreen(without_diagnostics,tune,pattern,std::nullopt,nullptr,host);
    CHECK_EQ(std::memcmp(actual.PixelData(),without_diagnostics.PixelData(),640*480*sizeof(Color)),0);
    std::filesystem::create_directories(BROTRACKER_BROWSER_SCREENSHOTS);
    CHECK(actual.SaveBMP(std::filesystem::path(BROTRACKER_BROWSER_SCREENSHOTS)/(std::string(name)+".bmp")));
    if(!confirming && !browser.View().names.empty()) {
        const auto row=browser.View().selected-browser.View().first;
        const auto pixel=actual.PixelData()[(kSampleBrowserLineHeight+row*kSampleBrowserLineHeight+1)*640+620];
        CHECK_EQ(pixel.r,153u);CHECK_EQ(pixel.g,153u);CHECK_EQ(pixel.b,153u); // Selection spans former diagnostics area.
    }
}
}
TEST_CASE(SampleBrowserRealMetadataShowsMonoAndRejectedStereoAndScrollsAt640By480) {
    SdPatternFiles backend;Begin(backend);
    for(unsigned i=0;i<40;++i){char name[80];std::snprintf(name,sizeof(name),"/Samples/sample_%02u.wav",i);FakePatternSd::files[name].bytes=Wave();}
    FakePatternSd::files["/Samples/z_stereo.WAV"].bytes=Wave(2);
    FakePatternSd::files["/Samples/ignore.txt"].bytes=Wave();FakePatternSd::files["/Samples/ignore.iff"].bytes=Wave();
    // Export the exact WAV bytes inspected through the firmware adapter so the
    // displayed metadata can also be checked independently against real files.
    const auto fixture_directory=std::filesystem::path(BROTRACKER_BROWSER_SCREENSHOTS)/"fixtures";
    std::filesystem::create_directories(fixture_directory);
    for(const auto name:{"sample_00.wav","z_stereo.WAV"}) {
        const auto& bytes=FakePatternSd::files[std::string("/Samples/")+name].bytes;
        std::ofstream file(fixture_directory/name,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());CHECK(file.good());
    }
    WavBrowser browser;List(backend,browser);CHECK_EQ(browser.View().names.size(),43u);CHECK_EQ(browser.View().rows,24u);
    CHECK_EQ(browser.View().names[0],std::string(".."));CHECK_EQ(browser.View().names[1],std::string("Nested/"));CHECK(browser.Message().empty());
    const auto read_count=FakePatternSd::reads.size();Highlight(browser,"sample_00.wav");
    CHECK_EQ(browser.Message(),std::string("PCM · 16-bit · 44.1 kHz"));Screenshot("mono-metadata-640x480",browser);
    std::string path;CHECK_EQ(browser.Select(path),2u);CHECK_EQ(path,std::string("Samples/sample_00.wav"));
    Highlight(browser,"z_stereo.WAV");CHECK(browser.View().first>0);CHECK_EQ(browser.Message(),std::string("Stereo WAV not supported"));
    path="previous draft";CHECK_EQ(browser.Select(path),0u);CHECK_EQ(path,std::string("previous draft"));Screenshot("stereo-rejected-scrolled-640x480",browser);
    Highlight(browser,"sample_39.wav");CHECK_EQ(browser.Message(),std::string("PCM · 16-bit · 44.1 kHz"));Screenshot("mono-scrolled-640x480",browser);
    Highlight(browser,"Nested/");CHECK(browser.Message().empty());Screenshot("directory-blank-status-640x480",browser);
    CHECK_EQ(FakePatternSd::reads.size(),read_count); // Highlight/render never touch SD.
    for(const auto& read:FakePatternSd::reads)CHECK(read.offset+read.count<=44u); // No PCM read.
    CHECK(SdAccess::AcquireWrite());SdAccess::ReleaseWrite(); // Listing released its lease.
}
TEST_CASE(SampleBrowserUsesExactValidatorRejectionMessagesAndKeepsSelectionOpen) {
    SdPatternFiles backend;Begin(backend);
    FakePatternSd::files["/Samples/a_stereo.wav"].bytes=Wave(2);
    FakePatternSd::files["/Samples/b_float.wav"].bytes=Wave(1,16,44100,3);
    FakePatternSd::files["/Samples/c_8bit.wav"].bytes=Wave(1,8);
    FakePatternSd::files["/Samples/d_rate.wav"].bytes=Wave(1,16,22050);
    FakePatternSd::files["/Samples/e_invalid.wav"].bytes={1,2,3};
    FakePatternSd::files["/Samples/f_read.wav"].bytes=Wave();FakePatternSd::negative_read_path="/Samples/f_read.wav";
    WavBrowser browser;List(backend,browser);
    const char* names[]={"a_stereo.wav","b_float.wav","c_8bit.wav","d_rate.wav","e_invalid.wav","f_read.wav"};
    const char* expected[]={"Stereo WAV not supported","WAV format not supported","WAV bit depth not supported","WAV sample rate not supported","Invalid WAV","Unable to read WAV"};
    for(unsigned i=0;i<6;++i){Highlight(browser,names[i]);CHECK_EQ(browser.Message(),std::string(expected[i]));std::string previous="saved.wav";CHECK_EQ(browser.Select(previous),0u);CHECK_EQ(previous,std::string("saved.wav"));CHECK_EQ(browser.Directory(),std::string("/Samples"));}
    Screenshot("read-error-640x480",browser);
}
TEST_CASE(SampleBrowserSingleLineEmptyNavigationErrorsAndReplacementConfirmation) {
    SdPatternFiles backend;Begin(backend);WavBrowser browser;List(backend,browser,"/Samples/Nested");
    CHECK_EQ(browser.Message(),std::string("No supported samples"));Screenshot("empty-640x480",browser);
    browser.Begin("/gone");browser.Fail(WavDirectoryResult::Missing);CHECK_EQ(browser.Message(),std::string("Folder not found"));CHECK_EQ(browser.Directory(),std::string("/Samples/Nested"));Screenshot("missing-folder-640x480",browser);
    browser.Begin("/bad");browser.Fail(WavDirectoryResult::ReadError);CHECK_EQ(browser.Message(),std::string("Unable to read folder"));Screenshot("folder-read-error-640x480",browser);
    Screenshot("replace-confirmation-640x480",browser,true,"Replace saved Sample WAV?\nOld: Samples/old.wav\nNew: Samples/new.wav\n\nEnter / A: stage replacement\nEscape / B: keep saved path");
}

TEST_CASE(SampleBrowserListsLongAndMixedCaseWavNamesButRetainsResourcePathValidation) {
    SdPatternFiles backend;Begin(backend);const auto long_name=std::string(80,'x')+".wav";
    FakePatternSd::files["/Samples/"+long_name].bytes=Wave();FakePatternSd::files["/Samples/with space.WaV"].bytes=Wave();
    FakePatternSd::files["/Samples/not-a-sample.txt"].bytes=Wave();WavBrowser browser;List(backend,browser);
    const auto read_count=FakePatternSd::reads.size();
    for(const auto& name:{long_name,std::string("with space.WaV")}) {
        Highlight(browser,name);CHECK_EQ(browser.Message(),std::string("WAV reference unavailable or invalid")); // Before Enter/A.
        const auto footer=browser.Message();std::string previous="previous.wav";
        CHECK_EQ(browser.Select(previous),0u);CHECK_EQ(previous,std::string("previous.wav"));CHECK_EQ(browser.Message(),footer);
    }
    CHECK_EQ(FakePatternSd::reads.size(),read_count);
    const auto directory="/"+std::string(55,'a');FakePatternSd::files[directory].directory=true;
    FakePatternSd::files[directory+"/test.wav"].bytes=Wave();FakePatternSd::files[directory+"/bad stereo.wav"].bytes=Wave(2);
    List(backend,browser,directory.c_str());Highlight(browser,"test.wav");
    const auto rejection=std::string("Cannot select test.wav: full SD-relative WAV path exceeds the supported 63-byte limit.");
    CHECK_EQ(browser.Message(),rejection); // Format-valid WAV, valid basename, unselectable full path.
    std::string previous="previous.wav";CHECK_EQ(browser.Select(previous),0u);CHECK_EQ(previous,std::string("previous.wav"));CHECK_EQ(browser.Message(),rejection);
    Highlight(browser,"bad stereo.wav");CHECK_EQ(browser.Message(),std::string("Stereo WAV not supported")); // Audio rejection takes precedence over filename/path.
    CHECK_EQ(browser.Select(previous),0u);CHECK_EQ(previous,std::string("previous.wav"));CHECK_EQ(browser.Message(),std::string("Stereo WAV not supported"));
}

TEST_CASE(SampleBrowserMetadataInspectionRespectsExistingChunkBudgetAndReadLease) {
    SdPatternFiles backend;Begin(backend);auto wave=Wave();std::vector<std::uint8_t> bytes(wave.begin(),wave.begin()+12);
    for(unsigned i=0;i<kWavInspectionChunkLimit+1;++i){const std::uint8_t chunk[8]={'J','U','N','K',0,0,0,0};bytes.insert(bytes.end(),chunk,chunk+8);}
    bytes.insert(bytes.end(),wave.begin()+12,wave.end());const auto length=bytes.size()-8;
    for(unsigned i=0;i<4;++i)bytes[4+i]=length>>(8*i);
    FakePatternSd::files["/Samples/budget.wav"].bytes=bytes;
    FakePatternSd::on_io=[](){CHECK(!SdAccess::AcquireWrite());};
    WavOpenRequest request;request.version=3;std::strcpy(request.directory,"/Samples");WavBrowsePage page;
    CHECK_EQ(ReadWavBrowsePage(backend,request,page),WavDirectoryResult::Ok);
    FakePatternSd::on_io=nullptr;CHECK_EQ(page.count,2u); // One directory and one WAV.
    CHECK(page.entries[0].kind==WavEntryKind::Directory);CHECK(page.entries[1].facts.status==WavBufferStatus::ChunkLimit);
    CHECK(FakePatternSd::reads.size()<=1+2*kWavInspectionChunkLimit);
    CHECK(SdAccess::AcquireWrite());SdAccess::ReleaseWrite();
}
