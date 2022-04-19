
set_project .
set_top_file sumi3.cpp
synthesize -simple-buffers=true -verbose
#cdfg
write_hdl

exit



