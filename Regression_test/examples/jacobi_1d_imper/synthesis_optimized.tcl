
set_project .
set_top_file jacobi_1d_imper.cpp
synthesize -verbose
set_period 5
optimize
write_hdl

exit



