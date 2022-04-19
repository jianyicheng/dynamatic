
set_project .
set_top_file atax.cpp
synthesize -simple-buffers=true -verbose
#cdfg
write_hdl

exit



