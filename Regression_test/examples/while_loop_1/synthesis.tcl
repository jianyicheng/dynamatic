
set_project .
set_top_file while_loop_1.cpp
synthesize -simple-buffers=true -verbose
#cdfg
write_hdl

exit



