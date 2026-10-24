include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
int main() { return 0; }" LUMOS_LIBC_IS_MUSL)

function(lumos_harden target)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${LUMOS_WERROR}>:/WX> /sdl /guard:cf /permissive- /utf-8)
    target_link_options(${target} PRIVATE /guard:cf)
    return()
  endif()

  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic $<$<BOOL:${LUMOS_WERROR}>:-Werror>
    -Wconversion -Wsign-conversion -Wshadow -Wnull-dereference
    -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual
    -fstack-protector-strong)
  target_compile_definitions(${target} PRIVATE
    _GLIBCXX_ASSERTIONS
    $<$<BOOL:${APPLE}>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>)
  target_compile_options(${target} PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>)

  if(LUMOS_SANITIZE)
    if(LUMOS_LIBC_IS_MUSL)
      target_compile_options(${target} PRIVATE
        -fsanitize=undefined -fsanitize-trap=undefined -fno-omit-frame-pointer)
    else()
      target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endfunction()
