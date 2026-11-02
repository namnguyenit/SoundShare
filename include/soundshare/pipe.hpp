#pragma once
#include <windows.h>
#include <string>
namespace soundshare {
class Control;
std::wstring pipe_path(const std::string& name);
void serve_pipe(Control& control,const std::string& name,HANDLE stop);
std::string pipe_request(const std::string& name,const std::string& request,HANDLE cancel=nullptr);
}
