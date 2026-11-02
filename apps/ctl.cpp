#include "soundshare/platform.hpp"
#include "soundshare/pipe.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int wmain(int argc,wchar_t** argv) {
    try {
        int index=1; std::string name=soundshare::current_pipe_name();
        if(argc>3 && std::wstring(argv[1])==L"--pipe") { name=soundshare::narrow(argv[2]); index=3; }
        if(index>=argc) { std::cout<<"SoundShare.Ctl [--pipe name] inventory|status|hello|shutdown|request JSON|request-file path\n"; return 2; }
        const auto command=soundshare::narrow(argv[index]); std::string request;
        if(command=="request" || command=="request-file") {
            if(index+1>=argc) throw std::runtime_error("request requires JSON or a file path");
            if(command=="request") request=soundshare::narrow(argv[index+1]);
            else {
                std::ifstream file(std::filesystem::path(argv[index+1]),std::ios::binary);
                if(!file) throw std::runtime_error("cannot open request file");
                request.assign(65537,'\0'); file.read(request.data(),static_cast<std::streamsize>(request.size()));
                request.resize(static_cast<std::size_t>(file.gcount()));
            }
        } else request=nlohmann::json{{"protocol",1},{"id",1},{"method",command}}.dump();
        const auto response=soundshare::pipe_request(name,request);
        const auto parsed=nlohmann::json::parse(response);
        std::cout<<parsed.dump(2)<<"\n"; return parsed.value("ok",false) ? 0 : 1;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 2; }
}
