
set_project .
set_top_file if_loop_mul.cpp
synthesize -verbose
set_period 4
optimize
write_hdl

exit



