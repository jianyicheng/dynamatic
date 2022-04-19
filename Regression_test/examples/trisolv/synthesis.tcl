
set_project .
set_top_file trisolv.cpp
synthesize -simple-buffers=true -verbose
#cdfg
write_hdl

exit



