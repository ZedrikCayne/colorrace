
./build.sh
#lldb -- build/colorrace --trace
#gdb --args build/colorrace --trace --auto-login
#gdb --args build/colorrace --trace
gdb --args build/colorrace --log-access logs/access-debug
#gdb --args build/colorrace --port 8443 --key secrets/key.pem --certificate secrets/certificate.pem --log-access --allow-non-routable
