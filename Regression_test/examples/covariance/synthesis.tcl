
set_project .
set_top_file covariance.cpp
synthesize -simple-buffers=true -verbose
#cdfg
write_hdl

exit



