
set_project .
set_top_file correlation_float.cpp
synthesize -simple-buffers=true -verbose
#cdfg
write_hdl

exit



