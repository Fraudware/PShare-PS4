// SPDX-License-Identifier: GPL-3.0-or-later
#include "http_body.hpp"
#include "pshare/core/media.hpp"
#include <cassert>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
int main() {
    using namespace pshare;
    char path[] = "/tmp/pshare-body-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    const std::string binary("a\0b\r\nc",6);
    assert(write(fd,binary.data(),binary.size())==static_cast<ssize_t>(binary.size())); close(fd);
    HttpRequest request;
    request.parts.push_back({"caption","hello",{},{},{}});
    request.parts.push_back({"file",{},path,"capture\".jpg","image/jpeg"});
    std::string body,type,error;
    assert(ps4::make_body(request,"boundary",body,type,error,nullptr));
    assert(type=="multipart/form-data; boundary=boundary");
    assert(body.find(binary)!=std::string::npos);
    assert(body.find("filename=\"capture\\\".jpg\"")!=std::string::npos);
    assert(body.ends_with("--boundary--\r\n"));
    // One allocation: the planned size is the exact multipart body size.
    assert(ps4::planned_size(request,"boundary")==body.size());
    assert(ps4::quoted("test\r\ninjected") == "testinjected");
    request.body=BodyKind::raw_file;request.raw_path=path;request.raw_type="application/octet-stream";
    assert(ps4::make_body(request,"b",body,type,error,nullptr) && body==binary);
    assert(ps4::planned_size(request,"b")==body.size());
    UploadProgress progress;progress.cancel=true;
    assert(!ps4::make_body(request,"b",body,type,error,&progress) && error=="Cancelled");
    fd=open(path,O_WRONLY); assert(fd>=0); assert(ftruncate(fd,ps4::kBodyLimit+1)==0); close(fd);
    assert(!ps4::make_body(request,"b",body,type,error,nullptr));
    assert(error.find("256 MiB")!=std::string::npos && body.empty());
    unlink(path);
    request.body=BodyKind::none;
    assert(ps4::make_body(request,"b",body,type,error,nullptr) && body.empty());
    assert(looks_like_title_id("CUSA12345"));
    assert(title_id_from_path("/pshare-user/av_contents/video/NPXS20001/CUSA12345/clip.mp4")=="CUSA12345");
    MediaKind kind;assert(capture_kind("clip.mp4",&kind)&&kind==MediaKind::video);
    std::cout << "PS4 body serialization, limits, cancellation and CUSA/MP4 tests passed\n";
}
