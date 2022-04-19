
set_project .
set_top_file while_loop_2.cpp
synthesize -verbose
optimize -timeout=60
write_hdl

exit



