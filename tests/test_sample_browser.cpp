#include "test_framework.h"
#include "ui/wav_browser.h"
#include "ui/pattern_screen.h"
#include "firmware/teensy/BroTracker/sd_pattern_storage.h"
#include <array>
#include <limits>
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
        const auto area=SampleBrowserListArea(browser.Directory());
        const auto pixel=actual.PixelData()[(area.top+row*area.row_height+1)*640+620];
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
    CHECK_EQ(browser.View().names[0],std::string(".."));CHECK_EQ(browser.View().names[1],std::string("Nested/"));CHECK(browser.SelectedMessage().empty());
    const auto read_count=FakePatternSd::reads.size();Highlight(browser,"sample_00.wav");
    CHECK_EQ(browser.SelectedMessage(),std::string("PCM \xc2\xb7 16-bit \xc2\xb7 44.1 kHz \xc2\xb7 Mono"));Screenshot("mono-metadata-640x480",browser);
    std::string path;CHECK_EQ(browser.Select(path),2u);CHECK_EQ(path,std::string("Samples/sample_00.wav"));
    Highlight(browser,"z_stereo.WAV");CHECK(browser.View().first>0);CHECK_EQ(browser.SelectedMessage(),std::string("PCM \xc2\xb7 16-bit \xc2\xb7 44.1 kHz \xc2\xb7 Stereo | Unsupported channels"));
    path="previous draft";CHECK_EQ(browser.Select(path),0u);CHECK_EQ(path,std::string("previous draft"));Screenshot("stereo-rejected-scrolled-640x480",browser);
    Highlight(browser,"sample_39.wav");CHECK_EQ(browser.SelectedMessage(),std::string("PCM \xc2\xb7 16-bit \xc2\xb7 44.1 kHz \xc2\xb7 Mono"));Screenshot("mono-scrolled-640x480",browser);
    Highlight(browser,"Nested/");CHECK(browser.SelectedMessage().empty());Screenshot("directory-blank-status-640x480",browser);
    CHECK_EQ(FakePatternSd::reads.size(),read_count); // Highlight/render never touch SD.
    for(const auto& read:FakePatternSd::reads)CHECK(read.offset+read.count<=44u); // No PCM read.
    CHECK(SdAccess::AcquireWrite());SdAccess::ReleaseWrite(); // Listing released its lease.
}
TEST_CASE(SampleBrowserUsesExactValidatorRejectionMessagesAndKeepsSelectionOpen) {
    SdPatternFiles backend;Begin(backend);
    FakePatternSd::files["/Samples/a_stereo.wav"].bytes=Wave(2);
    FakePatternSd::files["/Samples/b_float.wav"].bytes=Wave(1,32,44100,3);
    FakePatternSd::files["/Samples/c_8bit.wav"].bytes=Wave(1,8);
    FakePatternSd::files["/Samples/d_rate.wav"].bytes=Wave(1,16,22050);
    FakePatternSd::files["/Samples/e_invalid.wav"].bytes={1,2,3};
    FakePatternSd::files["/Samples/f_read.wav"].bytes=Wave();FakePatternSd::negative_read_path="/Samples/f_read.wav";
    WavBrowser browser;List(backend,browser);
    const char* names[]={"a_stereo.wav","b_float.wav","c_8bit.wav","d_rate.wav","e_invalid.wav","f_read.wav"};
    const char* expected[]={"PCM \xc2\xb7 16-bit \xc2\xb7 44.1 kHz \xc2\xb7 Stereo | Unsupported channels","IEEE float \xc2\xb7 32-bit \xc2\xb7 44.1 kHz \xc2\xb7 Mono | Unsupported encoding","PCM \xc2\xb7 8-bit \xc2\xb7 44.1 kHz \xc2\xb7 Mono | Unsupported bit depth","PCM \xc2\xb7 16-bit \xc2\xb7 22.05 kHz \xc2\xb7 Mono | Unsupported rate","Truncated WAV","Unable to read WAV"};
    for(unsigned i=0;i<6;++i){Highlight(browser,names[i]);CHECK_EQ(browser.SelectedMessage(),std::string(expected[i]));std::string previous="saved.wav";CHECK_EQ(browser.Select(previous),0u);CHECK_EQ(previous,std::string("saved.wav"));CHECK_EQ(browser.Directory(),std::string("/Samples"));}
    Screenshot("read-error-640x480",browser);
    Highlight(browser,"b_float.wav");Screenshot("unsupported-float-640x480",browser);
    Highlight(browser,"c_8bit.wav");Screenshot("unsupported-pcm8-640x480",browser);
}
TEST_CASE(SampleBrowserSingleLineEmptyNavigationErrorsAndReplacementConfirmation) {
    SdPatternFiles backend;Begin(backend);WavBrowser browser;List(backend,browser,"/Samples/Nested");
    CHECK_EQ(browser.SelectedMessage(),std::string("No supported samples"));Screenshot("empty-640x480",browser);
    browser.Begin("/gone");browser.Fail(WavDirectoryResult::Missing);CHECK_EQ(browser.SelectedMessage(),std::string("Folder not found"));CHECK_EQ(browser.Directory(),std::string("/Samples/Nested"));Screenshot("missing-folder-640x480",browser);
    browser.Begin("/bad");browser.Fail(WavDirectoryResult::ReadError);CHECK_EQ(browser.SelectedMessage(),std::string("Unable to read folder"));Screenshot("folder-read-error-640x480",browser);
    Screenshot("replace-confirmation-640x480",browser,true,"Replace saved Sample WAV?\nOld: Samples/old.wav\nNew: Samples/new.wav\n\nEnter / A: stage replacement\nEscape / B: keep saved path");
}

TEST_CASE(SampleBrowserListsLongAndMixedCaseWavNamesButRetainsResourcePathValidation) {
    SdPatternFiles backend;Begin(backend);const auto long_name=std::string(80,'x')+".wav";
    FakePatternSd::files["/Samples/"+long_name].bytes=Wave();FakePatternSd::files["/Samples/with space.WaV"].bytes=Wave();
    FakePatternSd::files["/Samples/not-a-sample.txt"].bytes=Wave();WavBrowser browser;List(backend,browser);
    const auto read_count=FakePatternSd::reads.size();
    for(const auto& name:{long_name,std::string("with space.WaV")}) {
        Highlight(browser,name);CHECK(browser.SelectedMessage().find("PCM")!=std::string::npos);
        std::string selected;CHECK_EQ(browser.Select(selected),2u);CHECK_EQ(selected,std::string("Samples/")+name);
    }
    CHECK_EQ(FakePatternSd::reads.size(),read_count);
    const auto directory="/"+std::string(248,'a');FakePatternSd::files[directory].directory=true;
    FakePatternSd::files[directory+"/test.wav"].bytes=Wave();FakePatternSd::files[directory+"/bad stereo.wav"].bytes=Wave(2);
    List(backend,browser,directory.c_str());Highlight(browser,"test.wav");
    const auto rejection=std::string("Cannot select test.wav: full SD-relative WAV path exceeds the supported 255-byte limit.");
    CHECK_EQ(browser.SelectedMessage(),rejection); // Format-valid WAV, valid basename, unselectable full path.
    std::string previous="previous.wav";CHECK_EQ(browser.Select(previous),0u);CHECK_EQ(previous,std::string("previous.wav"));CHECK_EQ(browser.SelectedMessage(),rejection);
    Highlight(browser,"bad stereo.wav");CHECK_EQ(browser.SelectedMessage(),std::string("PCM \xc2\xb7 16-bit \xc2\xb7 44.1 kHz \xc2\xb7 Stereo | Unsupported channels")); // Audio rejection takes precedence over filename/path.
    CHECK_EQ(browser.Select(previous),0u);CHECK_EQ(previous,std::string("previous.wav"));CHECK_EQ(browser.SelectedMessage(),std::string("PCM \xc2\xb7 16-bit \xc2\xb7 44.1 kHz \xc2\xb7 Stereo | Unsupported channels"));
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

TEST_CASE(SampleBrowserWrapsLongDirectoryAndBuildsSpacedPathWithoutTruncation) {
    SdPatternFiles backend;Begin(backend);
    const auto directory="/Samples/Jungle riddims/"+std::string(120,'d');FakePatternSd::files[directory].directory=true;
    FakePatternSd::files[directory+"/long sample.wav"].bytes=Wave();WavBrowser browser;List(backend,browser,directory.c_str());
    CHECK_EQ(browser.Directory(),directory);CHECK_EQ(browser.View().rows,23u);browser.Move(1);
    std::string selected;CHECK_EQ(browser.Select(selected),2u);CHECK_EQ(selected,directory.substr(1)+"/long sample.wav");
    Screenshot("full-long-spaced-directory-640x480",browser);
    browser.Move(-1);CHECK_EQ(browser.Select(selected),1u);CHECK_EQ(selected,std::string("/Samples/Jungle riddims"));
}

TEST_CASE(SampleBrowserShowsAvailableHeaderFactsWithoutMakingInvalidFilesSelectable) {
    WavBrowsePage page;
    std::strcpy(page.directory,"/Samples");
    page.done=true;page.count=7;page.scanned=7;
    const WavFileFacts facts[]={
        {WavBufferStatus::Success,11025,1,2,16,true},
        {WavBufferStatus::InvalidFormat,22050,1,2,16,true},
        {WavBufferStatus::IoError,8000,1,1,0,true},
        {WavBufferStatus::UnsupportedFormat,48001,0x00fe,6,0,true},
        {WavBufferStatus::UnsupportedFormat,96000,0xfffe,2,24,true},
        {WavBufferStatus::Truncated,0,0,0,0,true},
        {WavBufferStatus::UnsupportedFormat,44100,0x674f,1,16,true},
    };
    const char* text[]={
        "PCM \xc2\xb7 16-bit \xc2\xb7 11.025 kHz \xc2\xb7 Stereo",
        "PCM \xc2\xb7 16-bit \xc2\xb7 22.05 kHz \xc2\xb7 Stereo | Invalid WAV format header",
        "PCM \xc2\xb7 8.0 kHz \xc2\xb7 Mono | Unable to read WAV",
        "Tag 0x00FE \xc2\xb7 48.001 kHz \xc2\xb7 6 ch | Unsupported encoding",
        "Extensible \xc2\xb7 24-bit \xc2\xb7 96.0 kHz \xc2\xb7 Stereo | Unsupported encoding",
        "Truncated WAV",
        "Ogg Vorbis \xc2\xb7 16-bit \xc2\xb7 44.1 kHz \xc2\xb7 Mono | Unsupported encoding",
    };
    WavBrowser browser;browser.Begin("/Samples");
    for(unsigned i=0;i<page.count;++i) {
        std::snprintf(page.entries[i].name,sizeof(page.entries[i].name),"facts%u.wav",i);
        page.entries[i].facts=facts[i];
    }
    CHECK(browser.Accept(page));
    for(unsigned i=0;i<page.count;++i) {
        Highlight(browser,page.entries[i].name);
        CHECK_EQ(browser.SelectedMessage(),std::string(text[i]));
        CHECK(browser.Message().size()<=90u); // Even counting UTF-8 bytes, the audio footer fits.
        std::string path="unchanged.wav";
        CHECK_EQ(browser.Select(path),i==0?2u:0u);
        CHECK_EQ(path,std::string(i==0?"Samples/facts0.wav":"unchanged.wav"));
    }
}

TEST_CASE(PackedPcmBrowserRejectionReflectsNegotiatedBitDepth) {
    WavBrowsePage page;std::strcpy(page.directory,"/Samples");
    page.done=true;page.count=1;page.scanned=1;
    std::strcpy(page.entries[0].name,"tone.wav");
    auto& facts=page.entries[0].facts;
    facts={WavBufferStatus::UnsupportedFormat,48000,1,2,8,true};
    WavBrowser browser;
    for(bool pcm8:{false,true}) {
        browser.Begin("/Samples",pcm8?7:6);CHECK(browser.Accept(page));
        Highlight(browser,"tone.wav");
        CHECK(browser.SelectedMessage().find(pcm8?"Unsupported rate":"Unsupported bit depth")!=std::string::npos);
        std::string unchanged;CHECK_EQ(browser.Select(unchanged),0u);
    }
    facts={WavBufferStatus::Success,8363,1,2,8,true};
    browser.Begin("/Samples",7);CHECK(browser.Accept(page));Highlight(browser,"tone.wav");
    CHECK_EQ(browser.SelectedMessage(),std::string("PCM \xc2\xb7 8-bit \xc2\xb7 8.363 kHz \xc2\xb7 Stereo"));
    std::string chosen;CHECK_EQ(browser.Select(chosen),2u);CHECK_EQ(chosen,std::string("Samples/tone.wav"));
}

TEST_CASE(FileListNavigationClampsScrollsAndIgnoresEmptyRows) {
    FileListView view;
    const auto area=PatternLoadListArea();
    view.Move(std::numeric_limits<std::int64_t>::max());
    CHECK_EQ(view.selected,0u);CHECK_EQ(view.first,0u);
    CHECK(!area.EntryAt(view,area.left,area.top));
    view.names.resize(15);
    view.Move(13);
    CHECK_EQ(view.selected,13u);CHECK_EQ(view.first,12u);
    CHECK_EQ(area.EntryAt(view,area.left,area.top+2*area.row_height).value_or(99),14u);
    CHECK(!area.EntryAt(view,area.left,area.top+3*area.row_height));
    CHECK(!area.EntryAt(view,area.left,area.top-1));
    CHECK(!area.EntryAt(view,area.left+area.width,area.top));
    view.Move(std::numeric_limits<std::int64_t>::min());
    CHECK_EQ(view.selected,0u);CHECK_EQ(view.first,0u);
}

TEST_CASE(SampleBrowserEndKeepsItsScrolledSelectionWhenBatchesAppend) {
    WavBrowser browser;
    const std::string directory="/"+std::string(90,'a');
    const auto area=SampleBrowserListArea(directory);
    CHECK(area.top>kSampleBrowserLineHeight);
    browser.Begin(directory);
    WavBrowsePage page;
    std::strcpy(page.directory,directory.c_str());
    page.done=false;page.count=kPatternListPageSize;
    for(unsigned i=0;i<page.count;++i) {
        page.entries[i].kind=WavEntryKind::Directory;
        std::snprintf(page.entries[i].name,sizeof(page.entries[i].name),"folder_%02u",i);
    }
    for(unsigned batch=0;batch<4;++batch)CHECK(browser.Accept(page));
    browser.Move(std::numeric_limits<std::int64_t>::max());
    const auto selected=browser.View().selected,first=browser.View().first;
    CHECK_EQ(selected,4*kPatternListPageSize);
    CHECK(first>0u);
    CHECK_EQ(area.EntryAt(browser.View(),area.left,area.top+(selected-first)*area.row_height).value_or(99),selected);
    page.done=true;
    CHECK(browser.Accept(page));
    CHECK_EQ(browser.View().selected,selected);CHECK_EQ(browser.View().first,first);
    CHECK(!area.EntryAt(browser.View(),area.left,area.top+area.rows*area.row_height));
    browser.Move(std::numeric_limits<std::int64_t>::max());
    CHECK_EQ(browser.View().selected,5*kPatternListPageSize);
}
