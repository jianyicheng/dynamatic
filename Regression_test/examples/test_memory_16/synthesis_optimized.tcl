
set_project .
set_top_file test_memory_16.cpp
synthesize -verbose
optimize -timeout=60
write_hdl

exit



