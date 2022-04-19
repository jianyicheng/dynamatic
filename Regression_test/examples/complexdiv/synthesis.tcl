
set_project .
set_top_file complexdiv.cpp
synthesize -simple-buffers=true -verbose
#cdfg
write_hdl

exit



