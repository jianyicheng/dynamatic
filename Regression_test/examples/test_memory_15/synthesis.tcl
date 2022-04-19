
set_project .
set_top_file test_memory_15.cpp
synthesize -simple-buffers=true -verbose
#cdfg
write_hdl

exit



