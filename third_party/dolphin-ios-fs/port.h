/* SPDX-License-Identifier: MIT
 * wiillr's portable host binding for the separately licensed Dolphin backend. */
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using s32=int32_t;
#define ASSERT(x) assert(x)
#define ASSERT_MSG(category,x,...) assert(x)
#define ERROR_LOG_FMT(category,message,...) std::fprintf(stderr,"IOS FS: %s\n",message)
#define WARN_LOG_FMT(category,message,...) std::fprintf(stderr,"IOS FS: %s\n",message)
#define INFO_LOG_FMT(category,...) ((void)0)
#define PanicAlertFmt(message,...) throw std::runtime_error(message)
inline std::vector<std::string> SplitString(const std::string& s,char delimiter) {
  std::vector<std::string> result;
  size_t start=0;
  do { const size_t end=s.find(delimiter,start); result.push_back(s.substr(start,end-start));
    if(end==s.npos)break; start=end+1;
  } while(true);
  return result;
}
namespace Common {
template<class T> struct alignas(T) BigEndianValue {
  std::array<u8,sizeof(T)> bytes{};
  operator T()const {T value=0;for(u8 b:bytes)value=(value<<8)|b;return value;}
  BigEndianValue& operator=(T value) {for(size_t i=sizeof(T);i--;) {bytes[i]=u8(value);value>>=8;}return *this;}
};
template<class A,class B> auto DivideRoundingUp(A a,B b) {return (a+b-1)/b;}
inline bool IsPrintableCharacter(char c) {return u8(c)>=32 && u8(c)<127;}
std::string EscapePath(const std::string& path);
std::string UnescapeFileName(const std::string& name);
}
namespace File {
inline std::filesystem::path Utf8Path(const std::string& p){return std::filesystem::path(std::u8string(p.begin(),p.end()));}
enum class SeekOrigin {Begin};
class IOFile {
  FILE* file=nullptr;
public:
  IOFile()=default;
  IOFile(const std::string& path,const char* mode) {Open(path,mode);}
  IOFile(const IOFile&)=delete;
  IOFile& operator=(const IOFile&)=delete;
  IOFile(IOFile&& other):file(std::exchange(other.file,nullptr)){}
  ~IOFile(){if(file)std::fclose(file);}
  bool Open(const std::string& path,const char* mode) {
    if(file)std::fclose(file);
#ifdef _WIN32
    std::wstring wide_mode; while(*mode)wide_mode+=wchar_t(*mode++);
    file=_wfopen(Utf8Path(path).c_str(),wide_mode.c_str());
#else
    file=std::fopen(path.c_str(),mode);
#endif
    return file!=nullptr;
  }
  explicit operator bool()const{return file!=nullptr;}
  bool IsOpen()const{return file!=nullptr;}
  FILE* GetHandle()const{return file;}
  u64 GetSize()const {
    const auto old=std::ftell(file);if(old<0 || std::fseek(file,0,SEEK_END))throw std::runtime_error("file size");
    const auto size=std::ftell(file);if(size<0 || std::fseek(file,old,SEEK_SET))throw std::runtime_error("file size");
    return u64(size);
  }
  bool Seek(u64 offset,SeekOrigin) {return offset<=0x7fffffff && !std::fseek(file,long(offset),SEEK_SET);}
  template<class T> bool ReadArray(T* out,size_t n){return file && std::fread(out,sizeof(T),n,file)==n;}
  template<class T> bool WriteArray(const T* in,size_t n){return file && std::fwrite(in,sizeof(T),n,file)==n && !std::fflush(file);}
  bool WriteBytes(const void* in,size_t n){return WriteArray(static_cast<const u8*>(in),n);}
};
struct FSTEntry {bool isDirectory=false;std::string physicalName,virtualName;u64 size=0;std::vector<FSTEntry> children;};
class FileInfo {
  std::filesystem::file_status status;
public:
  explicit FileInfo(const std::string& path){std::error_code ec;status=std::filesystem::status(Utf8Path(path),ec);}
  bool Exists()const{return std::filesystem::exists(status);}
  bool IsFile()const{return std::filesystem::is_regular_file(status);}
  bool IsDirectory()const{return std::filesystem::is_directory(status);}
};
bool IsDirectory(const std::string&);bool IsFile(const std::string&);bool Exists(const std::string&);
bool CreateDir(const std::string&);bool CreateFullPath(const std::string&);bool CreateEmptyFile(const std::string&);
bool Delete(const std::string&);bool DeleteDirRecursively(const std::string&);bool Rename(const std::string&,const std::string&);
bool CopyRegularFile(const std::string&,const std::string&);u64 GetSize(const std::string&);
std::string GetTempFilenameForAtomicWrite(const std::string&);
FSTEntry ScanDirectoryTree(const std::string&,bool);
}
