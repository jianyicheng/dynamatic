
set_project .
set_top_file path_profiling.cpp
synthesize -verbose
set_period 5
optimize
write_hdl

exit



