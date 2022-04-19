
set_project .
set_top_file if_loop_4.cpp
synthesize -verbose
set_period 5
optimize
write_hdl

exit



