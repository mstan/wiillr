/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "port.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs=std::filesystem;
namespace Common {
// Dolphin's __xx__ filename encoding (Common/NandPaths.cpp), independently
// expressed here to keep existing extracted NAND names interoperable.
std::string EscapePath(const std::string& path) {
  std::string result;
  const auto parts=SplitString(path,'/');
  for(size_t n=0;n<parts.size();++n) {
    if(n)result+='/';
    const auto& part=parts[n];
    const bool dots=std::ranges::all_of(part,[](char c){return c=='.';});
    for(size_t i=0;i<part.size();++i) {
      const u8 c=u8(part[i]);
      if(c=='_' && i+1<part.size() && part[i+1]=='_') {result+="__5f____5f__";++i;continue;}
      if(dots ||
         c<=31 || c==127 || std::strchr("\"*/:<>?\\|",c)) {
        char escaped[7];std::snprintf(escaped,sizeof escaped,"__%02x__",c);result+=escaped;
      } else result+=char(c);
    }
  }
  return result;
}
std::string UnescapeFileName(const std::string& name) {
  std::string result=name;size_t at=0;
  while((at=result.find("__",at))!=result.npos) {
    if(at+6<=result.size() && result.substr(at+4,2)=="__") {
      unsigned c=0;const char* start=result.data()+at+2;
      const auto parsed=std::from_chars(start,start+2,c,16);
      if(parsed.ec==std::errc{} && parsed.ptr==start+2)result.replace(at,6,1,char(c));
    }
    ++at;
  }
  return result;
}
}
namespace File {
static fs::path path(const std::string& p){return Utf8Path(p);}
bool IsDirectory(const std::string& p){return FileInfo(p).IsDirectory();}
bool IsFile(const std::string& p){return FileInfo(p).IsFile();}
bool Exists(const std::string& p){return FileInfo(p).Exists();}
bool CreateDir(const std::string& p){std::error_code ec;return fs::create_directory(path(p),ec);}
bool CreateFullPath(const std::string& p){std::error_code ec;fs::create_directories(path(p),ec);return !ec;}
bool CreateEmptyFile(const std::string& p){IOFile f(p,"wb");return bool(f);}
bool Delete(const std::string& p){std::error_code ec;return fs::remove(path(p),ec);}
bool DeleteDirRecursively(const std::string& p){std::error_code ec;fs::remove_all(path(p),ec);return !ec;}
bool Rename(const std::string& a,const std::string& b){
#ifdef _WIN32
  return MoveFileExW(path(a).c_str(),path(b).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
  std::error_code ec;fs::rename(path(a),path(b),ec);return !ec;
#endif
}
bool CopyRegularFile(const std::string& a,const std::string& b){std::error_code ec;return fs::copy_file(path(a),path(b),ec);}
u64 GetSize(const std::string& p){std::error_code ec;auto n=fs::file_size(path(p),ec);return ec?0:n;}
std::string GetTempFilenameForAtomicWrite(const std::string& p){return p+".tmp";}
FSTEntry ScanDirectoryTree(const std::string& p,bool recursive) {
  FSTEntry result;result.physicalName=p;result.virtualName=path(p).filename().string();result.isDirectory=IsDirectory(p);
  if(!result.isDirectory){result.size=GetSize(p);return result;}
  for(const auto& child:fs::directory_iterator(path(p))) {
    if(child.is_symlink())throw std::runtime_error("NAND symlink");
    const auto raw=child.path().generic_u8string();const std::string name(raw.begin(),raw.end());
    FSTEntry item;
    if(recursive)item=ScanDirectoryTree(name,true);
    else {item.physicalName=name;item.virtualName=child.path().filename().string();item.isDirectory=child.is_directory();item.size=item.isDirectory?0:child.file_size();}
    result.size+=1+(item.isDirectory?item.size:0);result.children.push_back(std::move(item));
  }
  return result;
}
}
