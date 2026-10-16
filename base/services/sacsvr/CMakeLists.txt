add_executable(sacsvr sacsvr.c)
set_module_type(sacsvr win32cui UNICODE)
add_importlibs(sacsvr msvcrt kernel32 advapi32)
add_cd_file(TARGET sacsvr DESTINATION reactos/system32 FOR all)
