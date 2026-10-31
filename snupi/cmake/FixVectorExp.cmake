# Run after codegen and before compilation; leave unchanged files untouched.
file(GLOB generated_files "${GENERATED_DIR}/snupi_recomp.*.cpp")
set(total 0)
foreach(path IN LISTS generated_files)
  file(READ "${path}" original)
  string(REPLACE "\r\n" "\n" source "${original}")
  string(REGEX MATCHALL "// vexptefp(128)? v[0-9]+,v[0-9]+" ops "${source}")
  if(NOT ops)
    continue()
  endif()
  list(LENGTH ops count)
  math(EXPR total "${total} + ${count}")
  string(REGEX REPLACE
    "// vexptefp(128)? (v[0-9]+),(v[0-9]+)\n([ \t]*ctx.fpscr.enableFlushMode[^\n]*\n)?[ \t]*\\{[^}]*\\}"
    "// vexptefp\\1 \\2,\\3\n\tctx.fpscr.enableFlushModeUnconditional();\n\tsnupi::Exp2Estimate(ctx.\\2.f32, ctx.\\3.f32);"
    source "${source}")
  string(REGEX MATCHALL "snupi::Exp2Estimate\\(ctx.v[0-9]+.f32, ctx.v[0-9]+.f32\\)" fixed "${source}")
  list(LENGTH fixed fixed_count)
  if(NOT fixed_count EQUAL count)
    message(FATAL_ERROR "Unrecognized vexptefp codegen in ${path}; review the math fix for this SDK")
  endif()
  if(NOT source MATCHES "#include \"src/ppc_math_fixes.h\"")
    string(PREPEND source "#include \"src/ppc_math_fixes.h\"\n")
  endif()
  if(NOT source STREQUAL original)
    file(WRITE "${path}" "${source}")
  endif()
endforeach()
message(STATUS "Validated ${total} vector exponential fixes")


