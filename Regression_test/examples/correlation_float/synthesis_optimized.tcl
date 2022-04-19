
set_project .
set_top_file correlation_float.cpp
synthesize -verbose
set_period 5
optimize -timeout=120
write_hdl

exit



