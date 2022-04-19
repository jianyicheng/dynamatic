
set_project .
set_top_file share_test_1.cpp
synthesize -verbose
set_period 5
optimize
write_hdl

exit



