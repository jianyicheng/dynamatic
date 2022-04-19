
set_project .
set_top_file sumi3.cpp
synthesize -verbose
set_period 5
optimize
write_hdl

exit



