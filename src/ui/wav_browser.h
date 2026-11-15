#pragma once
#include "file_list.h"
#include "bringup/wav_directory.h"
// Shared host model: only a complete, correlated Teensy listing changes the
// visible directory. Failed navigation preserves the last committed list.
class WavBrowser {
public:
    void Reset(){*this=WavBrowser{};}
    const std::string& Directory()const{return directory_;}
    const std::string& Requested()const{return requested_;}
    const FileListView& View()const{return view_;}
    bool Loading()const{return loading_;}
    std::string Message()const {
        if(!message_.empty())return message_;
        if(loading_)return {};
        if(entries_.empty())return "No supported samples";
        const auto* entry=SelectedFile();if(!entry)return {};
        const auto& f=entry->facts;
        if(!f.known || f.status==BroTracker::WavBufferStatus::IoError)return "Unable to read WAV";
        if(f.status==BroTracker::WavBufferStatus::UnsupportedFormat) {
            if(f.format!=1)return "WAV format not supported";
            if(f.channels==2)return "Stereo WAV not supported";
            if(f.channels!=1)return "WAV format not supported";
            if(f.bits!=16)return "WAV bit depth not supported";
            if(f.sample_rate!=44100)return "WAV sample rate not supported";
        }
        if(f.status!=BroTracker::WavBufferStatus::Success)return "Invalid WAV";
        char resource[64];const auto rejection=ValidateResource(*entry,resource);if(!rejection.empty())return rejection;
        char rate[32];std::snprintf(rate,sizeof(rate),"%u.%u kHz",f.sample_rate/1000,(f.sample_rate%1000)/100);
        return std::string("PCM ")+"\xc2\xb7 "+std::to_string(f.bits)+"-bit "+"\xc2\xb7 "+rate;
    }
    void Begin(std::string path){requested_=std::move(path);pending_.clear();pending_directory_.clear();seen_=0;fallback_=0;loading_=true;message_.clear();}
    bool Accept(const BroTracker::WavBrowsePage& page) {
        if(!loading_ || !BroTracker::ValidWavDirectory(page.directory) || page.count>BroTracker::kPatternListPageSize || page.fallback>2)return false;
        if(!pending_directory_.empty() && pending_directory_!=page.directory)return false;
        pending_directory_=page.directory;if(page.fallback)fallback_=page.fallback;seen_+=page.scanned;
        for(unsigned i=0;i<page.count;++i)pending_.push_back(page.entries[i]);
        if(!page.done)return true;
        std::sort(pending_.begin(),pending_.end(),[](const auto& a,const auto& b){return a.kind!=b.kind?a.kind==BroTracker::WavEntryKind::Directory:std::strcmp(a.name,b.name)<0;});
        entries_=pending_;directory_=pending_directory_;view_={};view_.rows=SampleBrowserRows(480);
        if(directory_!="/")view_.names.push_back("..");
        for(const auto& entry:entries_) {
            std::string label=entry.name;
            if(entry.kind==BroTracker::WavEntryKind::Directory)label+='/';
            view_.names.push_back(label);
        }
        loading_=false;
        message_=fallback_?(fallback_==1?"Folder not found":"Unable to read folder"):std::string{};
        return true;
    }
    void Fail(BroTracker::WavDirectoryResult result) {
        loading_=false;pending_.clear();
        message_=result==BroTracker::WavDirectoryResult::Missing?"Folder not found":"Unable to read folder";
    }
    void Move(int direction){if(!loading_){message_.clear();view_.Move(direction);}}
    // 0=no action/error, 1=directory, 2=file. Paths come from the confirmed CWD.
    unsigned Select(std::string& path) {
        if(loading_ || view_.names.empty() || view_.selected>=view_.names.size())return 0;
        if(directory_!="/" && view_.selected==0) {char parent[64];std::strcpy(parent,directory_.c_str());BroTracker::ParentWavDirectory(parent);path=parent;return 1;}
        const auto index=view_.selected-(directory_!="/"?1:0);if(index>=entries_.size())return 0;
        const auto& entry=entries_[index];
        if(entry.kind==BroTracker::WavEntryKind::Directory) {
            path=(directory_=="/"?directory_:directory_+"/")+entry.name;
            if(!BroTracker::ValidWavDirectory(path)) {message_="Cannot open "+path+": folder path exceeds 63 bytes or uses unsupported characters. Kept "+directory_+".";return 0;}
            return 1;
        }
        if(!entry.facts.known || entry.facts.status!=BroTracker::WavBufferStatus::Success)return 0;
        char resource[64];const auto rejection=ValidateResource(entry,resource);
        if(!rejection.empty()){message_=rejection;return 0;}
        path=resource;return 2;
    }
private:
    std::string ValidateResource(const BroTracker::WavDirectoryEntry& entry,char (&resource)[64])const {
        if(!BroTracker::CompatibleWavName(entry.name))return "WAV reference unavailable or invalid";
        if(!BroTracker::BuildWavResourceAt(directory_,entry.name,resource,sizeof(resource)))
            return "Cannot select "+std::string(entry.name)+": full SD-relative WAV path exceeds the supported 63-byte limit.";
        return {};
    }
    const BroTracker::WavDirectoryEntry* SelectedFile()const {
        if(view_.names.empty() || view_.selected>=view_.names.size() || (directory_!="/" && view_.selected==0))return nullptr;
        const auto index=view_.selected-(directory_!="/"?1:0);
        return index<entries_.size() && entries_[index].kind==BroTracker::WavEntryKind::File?&entries_[index]:nullptr;
    }
    std::string directory_,requested_,pending_directory_,message_;
    FileListView view_;std::vector<BroTracker::WavDirectoryEntry> entries_,pending_;
    unsigned seen_=0,fallback_=0;bool loading_=false;
};
