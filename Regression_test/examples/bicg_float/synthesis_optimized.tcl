
set_project .
set_top_file bicg_float.cpp
synthesize -use-lsq=false -verbose
optimize -timeout=120
write_hdl

exit



