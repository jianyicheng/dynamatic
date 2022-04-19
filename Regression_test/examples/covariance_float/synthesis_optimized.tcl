
set_project .
set_top_file covariance_float.cpp
synthesize -verbose
set_period 5
optimize -timeout=60
write_hdl

exit



