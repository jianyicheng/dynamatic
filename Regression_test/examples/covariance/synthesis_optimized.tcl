
set_project .
set_top_file covariance.cpp
synthesize -verbose
optimize -timeout=60
write_hdl

exit



