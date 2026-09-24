#include <cstdlib>
#include <functional>
#include <mutex>
#include <new>
#include <xutility>

namespace std {

[[noreturn]] void __cdecl _Xbad_alloc() { abort(); }
[[noreturn]] void __cdecl _Xinvalid_argument(const char*) { abort(); }
[[noreturn]] void __cdecl _Xlength_error(const char*) { abort(); }
[[noreturn]] void __cdecl _Xout_of_range(const char*) { abort(); }
[[noreturn]] void __cdecl _Xoverflow_error(const char*) { abort(); }
[[noreturn]] void __cdecl _Xruntime_error(const char*) { abort(); }
[[noreturn]] void __cdecl _Xbad_function_call() { abort(); }
[[noreturn]] void __cdecl _Throw_Cpp_error(int) { abort(); }
[[noreturn]] void __cdecl _Throw_C_error(int) { abort(); }

}

void* __cdecl operator new(size_t size) {
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
