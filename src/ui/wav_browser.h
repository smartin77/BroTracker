#pragma once
#include <cstdio>
#include "file_list.h"
#include "bringup/wav_directory.h"
// Publish correlated batches from one confirmed directory in scan order.
// Appending never changes the highlight or scroll position; failed navigation
// retains the last confirmed directory until the first new batch arrives.
class WavBrowser {
public:
    void Reset(){*this=WavBrowser{};}
    const std::string& Directory()const{return directory_;}
    const std::string& Requested()const{return requested_;}
    const FileListView& View()const{return view_;}
    bool Loading()const{return awaiting_first_;}
    bool Reading()const{return loading_;}
    unsigned SamplesRead()const{return samples_;}
    std::string Message()const {
        const auto detail=SelectedMessage();
        const auto count=std::to_string(samples_)+" samples read";
        return detail.empty()?count:count+" | "+detail;
    }
    std::string SelectedMessage()const {
        if(!message_.empty())return message_;
        if(awaiting_first_)return {};
        if(entries_.empty() && !loading_)return "No supported samples";
        const auto* entry=SelectedFile();if(!entry)return {};
        const auto& f=entry->facts;
        if(!f.known)return "Unable to read WAV";
        if(f.status==BroTracker::WavBufferStatus::Success) {
            char resource[BroTracker::kSamplePathMaximum+1];
            const auto rejection=ValidateResource(*entry,resource);
            if(!rejection.empty())return rejection;
        }
        auto detail=FormatFacts(f);
        const auto* reason=FormatRejection(f);
        if(*reason) {
            if(!detail.empty())detail+=" | ";
            detail+=reason;
        }
        return detail;
    }
    void Begin(std::string path,bool pcm8_and_8363=false){pcm8_and_8363_=pcm8_and_8363;requested_=std::move(path);pending_directory_.clear();seen_=0;fallback_=0;loading_=awaiting_first_=true;message_.clear();}
    bool Accept(const BroTracker::WavBrowsePage& page) {
        if(!loading_ || !BroTracker::ValidWavDirectory(page.directory) || page.count>BroTracker::kPatternListPageSize || page.fallback>2)return false;
        if(!pending_directory_.empty() && pending_directory_!=page.directory)return false;
        if(awaiting_first_) {
            entries_.clear();samples_=0;directory_=page.directory;pending_directory_=page.directory;
            view_={};view_.rows=SampleBrowserRows(480,std::max<std::size_t>(1,(directory_.size()+89)/90));
            if(directory_!="/")view_.names.push_back("..");
            awaiting_first_=false;
        }
        if(page.fallback)fallback_=page.fallback;seen_+=page.scanned;
        for(unsigned i=0;i<page.count;++i) {
            const auto& entry=page.entries[i];entries_.push_back(entry);
            std::string label=entry.name;
            if(entry.kind==BroTracker::WavEntryKind::Directory)label+='/';else ++samples_;
            view_.names.push_back(label);
        }
        loading_=!page.done;
        if(page.fallback)message_=fallback_==1?"Folder not found":"Unable to read folder";
        return true;
    }
    void Fail(BroTracker::WavDirectoryResult result) {
        loading_=awaiting_first_=false;
        message_=result==BroTracker::WavDirectoryResult::Missing?"Folder not found":"Unable to read folder";
    }
    void Move(int direction){if(!awaiting_first_){message_.clear();view_.Move(direction);}}
    // 0=no action/error, 1=directory, 2=file. Paths come from the confirmed CWD.
    unsigned Select(std::string& path) {
        if(awaiting_first_ || view_.names.empty() || view_.selected>=view_.names.size())return 0;
        if(directory_!="/" && view_.selected==0) {char parent[BroTracker::kSamplePathMaximum+1];std::strcpy(parent,directory_.c_str());BroTracker::ParentWavDirectory(parent);path=parent;return 1;}
        const auto index=view_.selected-(directory_!="/"?1:0);if(index>=entries_.size())return 0;
        const auto& entry=entries_[index];
        if(entry.kind==BroTracker::WavEntryKind::Directory) {
            path=(directory_=="/"?directory_:directory_+"/")+entry.name;
            if(!BroTracker::ValidWavDirectory(path)) {message_="Cannot open "+path+": folder path exceeds 255 bytes or uses unsupported characters. Kept "+directory_+".";return 0;}
            return 1;
        }
        if(!entry.facts.known || entry.facts.status!=BroTracker::WavBufferStatus::Success)return 0;
        char resource[BroTracker::kSamplePathMaximum+1];const auto rejection=ValidateResource(entry,resource);
        if(!rejection.empty()){message_=rejection;return 0;}
        path=resource;return 2;
    }
private:
    static std::string EncodingName(unsigned tag) {
        switch(tag) {
            case 0:return {};
            case 1:return "PCM";
            case 2:return "MS ADPCM";
            case 3:return "IEEE float";
            case 6:return "A-law";
            case 7:return "Mu-law";
            case 0x11:return "IMA ADPCM";
            case 0x31:return "GSM 6.10";
            case 0x55:return "MP3 in WAV";
            case 0x674f:case 0x6750:case 0x6751:
            case 0x676f:case 0x6770:case 0x6771:return "Ogg Vorbis";
            case 0xfffe:return "Extensible";
        }
        char name[12];
        std::snprintf(name,sizeof(name),"Tag 0x%04X",tag);
        return name;
    }
    static std::string FormatFacts(const BroTracker::WavFileFacts& f) {
        auto detail=EncodingName(f.format);
        const auto append=[&](const std::string& value) {
            if(!detail.empty())detail+=" \xc2\xb7 ";
            detail+=value;
        };
        // The current wire format uses zero for fields unavailable to the inspector.
        if(f.bits)append(std::to_string(f.bits)+"-bit");
        if(f.sample_rate) {
            auto fraction=std::to_string(1000+f.sample_rate%1000).substr(1);
            while(fraction.size()>1 && fraction.back()=='0')fraction.pop_back();
            append(std::to_string(f.sample_rate/1000)+"."+fraction+" kHz");
        }
        if(f.channels==1)append("Mono");
        else if(f.channels==2)append("Stereo");
        else if(f.channels)append(std::to_string(f.channels)+" ch");
        return detail;
    }
    const char* FormatRejection(const BroTracker::WavFileFacts& f) const {
        using BroTracker::WavBufferStatus;
        switch(f.status) {
            case WavBufferStatus::Success:return "";
            case WavBufferStatus::IoError:return "Unable to read WAV";
            case WavBufferStatus::ChunkLimit:return "WAV inspection limit";
            case WavBufferStatus::UnsupportedFormat:
                if(f.format && f.format!=1)return "Unsupported encoding";
                if(f.bits && f.bits!=16 && !(pcm8_and_8363_ && f.bits==8))return "Unsupported bit depth";
                // Older negotiated browser versions can reject a lower rate or stereo.
                if(f.sample_rate && f.sample_rate!=44100)return "Unsupported rate";
                if(f.channels && f.channels!=1)return "Unsupported channels";
                return "Unsupported WAV";
            default:return "Invalid WAV";
        }
    }
    std::string ValidateResource(const BroTracker::WavDirectoryEntry& entry,char (&resource)[BroTracker::kSamplePathMaximum+1])const {
        if(!BroTracker::SampleWavName(entry.name))return "WAV reference unavailable or invalid";
        if(!BroTracker::BuildWavResourceAt(directory_,entry.name,resource,sizeof(resource)))
            return "Cannot select "+std::string(entry.name)+": full SD-relative WAV path exceeds the supported 255-byte limit.";
        return {};
    }
    const BroTracker::WavDirectoryEntry* SelectedFile()const {
        if(view_.names.empty() || view_.selected>=view_.names.size() || (directory_!="/" && view_.selected==0))return nullptr;
        const auto index=view_.selected-(directory_!="/"?1:0);
        return index<entries_.size() && entries_[index].kind==BroTracker::WavEntryKind::File?&entries_[index]:nullptr;
    }
    std::string directory_,requested_,pending_directory_,message_;
    FileListView view_;std::vector<BroTracker::WavDirectoryEntry> entries_;
    unsigned seen_=0,fallback_=0,samples_=0;bool loading_=false,awaiting_first_=false,pcm8_and_8363_=false;
};
